/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
//
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
//
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
//
#include "oomph_libfabric_defines.hpp"
#include "fabric_error.hpp"
#include "locality.hpp"
#include "memory_region.hpp"
#include "operation_context.hpp"
#include "controller_base.hpp"
//
#include <oomph/util/unique_function.hpp>
//
#include <mpi.h>

namespace NS_DEBUG
{
// cppcheck-suppress ConfigurationNotChecked
static debug::enable_print<true> cnt_deb("CONTROL");
static debug::enable_print<true> cnt_err("CONTROL");
} // namespace NS_DEBUG

namespace oomph::libfabric
{

class controller : public controller_base<controller>
{
  public:
    // --------------------------------------------------------------------
    controller()
    : controller_base()
    {
    }

    // --------------------------------------------------------------------
    void initialize_derived(std::string const&, bool, int, size_t, MPI_Comm mpi_comm)
    {
        // Broadcast address of all endpoints to all ranks
        // and fill address vector with info
        exchange_addresses(av_, mpi_comm);
    }

    // --------------------------------------------------------------------
    constexpr fi_threading threadlevel_flags()
    {
#if defined(HAVE_LIBFABRIC_GNI) /*|| defined(HAVE_LIBFABRIC_CXI)*/
        return FI_THREAD_ENDPOINT;
#else
        return FI_THREAD_SAFE;
#endif
    }

    // --------------------------------------------------------------------
    constexpr uint64_t caps_flags()
    {
#if OOMPH_ENABLE_DEVICE && !defined(HAVE_LIBFABRIC_TCP)
        std::int64_t hmem_flags = FI_HMEM;
#else
        std::int64_t hmem_flags = 0;
#endif
        return hmem_flags | FI_MSG | FI_TAGGED | FI_RMA | FI_READ | FI_WRITE | FI_RECV | FI_SEND |
               FI_TRANSMIT | FI_REMOTE_READ | FI_REMOTE_WRITE;
    }

    // --------------------------------------------------------------------
    // we do not need to perform any special actions on init (to contact root node)
    void setup_root_node_address(struct fi_info* /*info*/) {}

    // --------------------------------------------------------------------
    // send address to rank 0 and receive array of all localities
    void MPI_exchange_localities(fid_av* av, MPI_Comm comm, int rank, int size)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnt_deb.scope(NS_DEBUG::ptr(this), __func__);
        std::vector<char>     localities(size * locality_defs::array_size, 0);
        //
        if (rank > 0)
        {
            LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("sending here"), iplocality(here_), "size",
                                          locality_defs::array_size));
            /*int err = */ MPI_Send(here_.fabric_data(), locality_defs::array_size, MPI_CHAR,
                0, // dst rank
                0, // tag
                comm);

            LF_DEB(NS_DEBUG::cnt_deb,
                debug(debug::str<>("receiving all"), "size", locality_defs::array_size));

            MPI_Status status;
            /*err = */ MPI_Recv(localities.data(), size * locality_defs::array_size, MPI_CHAR,
                0, // src rank
                0, // tag
                comm, &status);
            LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("received addresses")));
        }
        else
        {
            LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("receiving addresses")));
            memcpy(&localities[0], here_.fabric_data(), locality_defs::array_size);
            for (int i = 1; i < size; ++i)
            {
                LF_DEB(NS_DEBUG::cnt_deb,
                    debug(debug::str<>("receiving address"), debug::dec<>(i)));
                MPI_Status status;
                /*int err = */ MPI_Recv(&localities[i * locality_defs::array_size],
                    size * locality_defs::array_size, MPI_CHAR,
                    i, // src rank
                    0, // tag
                    comm, &status);
                LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("received address"), debug::dec<>(i)));
            }

            LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("sending all")));
            for (int i = 1; i < size; ++i)
            {
                LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("sending to"), debug::dec<>(i)));
                /*int err = */ MPI_Send(&localities[0], size * locality_defs::array_size, MPI_CHAR,
                    i, // dst rank
                    0, // tag
                    comm);
            }
        }

        // all ranks should now have a full localities vector
        LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("populating vector")));
        for (int i = 0; i < size; ++i)
        {
            locality temp;
            int      offset = i * locality_defs::array_size;
            memcpy(temp.fabric_data_writable(), &localities[offset], locality_defs::array_size);
            insert_address(av, temp);
        }
    }

    // --------------------------------------------------------------------
    // if we did not bootstrap, then fetch the list of all localities
    // and insert each one into the address vector
    void exchange_addresses(fid_av* av, MPI_Comm mpi_comm)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnt_deb.scope(NS_DEBUG::ptr(this), __func__);

        int rank, size;
        MPI_Comm_rank(mpi_comm, &rank);
        MPI_Comm_size(mpi_comm, &size);

        LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("initialize_localities"), size, "localities"));

        MPI_exchange_localities(av, mpi_comm, rank, size);
        debug_print_av_vector(size);
        LF_DEB(NS_DEBUG::cnt_deb, debug(debug::str<>("Done localities")));
    }

    // --------------------------------------------------------------------
    inline constexpr bool bypass_tx_lock()
    {
#if defined(HAVE_LIBFABRIC_GNI)
        return true;
#elif defined(HAVE_LIBFABRIC_CXI)
        // @todo : cxi provider is not yet thread safe using scalable endpoints
        return false;
#else
        return (threadlevel_flags() == FI_THREAD_SAFE ||
                endpoint_type_ == endpoint_type::threadlocalTx);
#endif
    }

    // --------------------------------------------------------------------
    inline controller_base::unique_lock get_tx_lock()
    {
        if (bypass_tx_lock()) return unique_lock();
        return unique_lock(send_mutex_);
    }

    // --------------------------------------------------------------------
    inline controller_base::unique_lock try_tx_lock()
    {
        if (bypass_tx_lock()) return unique_lock();
        return unique_lock(send_mutex_, std::try_to_lock_t{});
    }

    // --------------------------------------------------------------------
    inline constexpr bool bypass_rx_lock()
    {
#ifdef HAVE_LIBFABRIC_GNI
        return true;
#else
        return (
            threadlevel_flags() == FI_THREAD_SAFE || endpoint_type_ == endpoint_type::scalableTxRx);
#endif
    }

    // --------------------------------------------------------------------
    inline controller_base::unique_lock get_rx_lock()
    {
        if (bypass_rx_lock()) return unique_lock();
        return std::move(unique_lock(recv_mutex_));
    }

    // --------------------------------------------------------------------
    inline controller_base::unique_lock try_rx_lock()
    {
        if (bypass_rx_lock()) return unique_lock();
        return unique_lock(recv_mutex_, std::try_to_lock_t{});
    }

    // --------------------------------------------------------------------
    int poll_send_queue(fid_cq* send_cq, void* user_data)
    {
#ifdef EXCESSIVE_POLLING_BACKOFF_MICRO_S
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::microseconds>(now - send_poll_stamp).count() <
            EXCESSIVE_POLLING_BACKOFF_MICRO_S)
            return 0;
        send_poll_stamp = now;
#endif
        int             ret;
        fi_cq_msg_entry entry[max_completions_array_limit_];
        assert(max_completions_per_poll_ <= max_completions_array_limit_);
        {
            auto lock = try_tx_lock();

            // if we're not threadlocal and didn't get the lock,
            // then another thread is polling now, just exit
            if (!bypass_tx_lock() && !lock.owns_lock()) { return -1; }

            static auto polling = NS_DEBUG::cnt_deb.make_timer(1, debug::str<>("poll send queue"));
            LF_DEB(NS_DEBUG::cnt_deb, timed(polling, NS_DEBUG::ptr(send_cq)));

            // poll for completions
            {
                ret = fi_cq_read(send_cq, &entry[0], max_completions_per_poll_);
            }
            // if there is an error, retrieve it
            if (ret == -FI_EAVAIL)
            {
                struct fi_cq_err_entry e = {};
                int                    err_sz = fi_cq_readerr(send_cq, &e, 0);
                (void)err_sz;

                // flags might not be set correctly
                if ((e.flags & (FI_MSG | FI_SEND | FI_TAGGED)) != 0)
                {
                    NS_DEBUG::cnt_err.error("txcq Error FI_EAVAIL for "
                                            "FI_SEND with len",
                        debug::hex<6>(e.len), "context", NS_DEBUG::ptr(e.op_context), "code",
                        NS_DEBUG::dec<3>(e.err), "flags", debug::bin<16>(e.flags), "error",
                        fi_cq_strerror(send_cq, e.prov_errno, e.err_data, (char*)e.buf, e.len));
                }
                else if ((e.flags & FI_RMA) != 0)
                {
                    NS_DEBUG::cnt_err.error("txcq Error FI_EAVAIL for "
                                            "FI_RMA with len",
                        debug::hex<6>(e.len), "context", NS_DEBUG::ptr(e.op_context), "code",
                        NS_DEBUG::dec<3>(e.err), "flags", debug::bin<16>(e.flags), "error",
                        fi_cq_strerror(send_cq, e.prov_errno, e.err_data, (char*)e.buf, e.len));
                }
                operation_context* handler = reinterpret_cast<operation_context*>(e.op_context);
                handler->handle_error(e);
                return 0;
            }
        }
        //
        // exit possibly locked region and process each completion
        //
        if (ret > 0)
        {
            int processed = 0;
            for (int i = 0; i < ret; ++i)
            {
                ++sends_complete;
                LF_DEB(NS_DEBUG::cnt_deb,
                    debug(debug::str<>("Completion"), i, debug::dec<2>(i), "txcq flags",
                        fi_tostr(&entry[i].flags, FI_TYPE_CQ_EVENT_FLAGS), "(",
                        debug::dec<>(entry[i].flags), ")", "context",
                        NS_DEBUG::ptr(entry[i].op_context), "length", debug::hex<6>(entry[i].len)));
                if ((entry[i].flags & (FI_TAGGED | FI_SEND | FI_MSG)) != 0)
                {
                    LF_DEB(NS_DEBUG::cnt_deb,
                        debug(debug::str<>("Completion"), "txcq tagged send completion",
                            NS_DEBUG::ptr(entry[i].op_context)));

                    operation_context* handler =
                        reinterpret_cast<operation_context*>(entry[i].op_context);
                    processed += handler->handle_tagged_send_completion(user_data);
                }
                else
                {
                    NS_DEBUG::cnt_err.error("Received an unknown txcq completion",
                        debug::dec<>(entry[i].flags), debug::bin<64>(entry[i].flags));
                    std::terminate();
                }
            }
            return processed;
        }
        else if (ret == 0 || ret == -FI_EAGAIN)
        {
            // do nothing, we will try again on the next check
        }
        else { NS_DEBUG::cnt_err.error("unknown error in completion txcq read"); }
        return 0;
    }

    // --------------------------------------------------------------------
    int poll_recv_queue(fid_cq* rx_cq, void* user_data)
    {
#ifdef EXCESSIVE_POLLING_BACKOFF_MICRO_S
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::microseconds>(now - recv_poll_stamp).count() <
            EXCESSIVE_POLLING_BACKOFF_MICRO_S)
            return 0;
        recv_poll_stamp = now;
#endif
        int             ret;
        fi_cq_msg_entry entry[max_completions_array_limit_];
        assert(max_completions_per_poll_ <= max_completions_array_limit_);
        {
            auto lock = get_rx_lock();

            // if we're not threadlocal and didn't get the lock,
            // then another thread is polling now, just exit
            if (!bypass_rx_lock() && !lock.owns_lock()) { return -1; }

            static auto polling = NS_DEBUG::cnt_deb.make_timer(1, debug::str<>("poll recv queue"));
            LF_DEB(NS_DEBUG::cnt_deb, timed(polling, NS_DEBUG::ptr(rx_cq)));

            // poll for completions
            {
                ret = fi_cq_read(rx_cq, &entry[0], max_completions_per_poll_);
            }
            // if there is an error, retrieve it
            if (ret == -FI_EAVAIL)
            {
                // read the full error status
                struct fi_cq_err_entry e = {};
                int                    err_sz = fi_cq_readerr(rx_cq, &e, 0);
                (void)err_sz;
                // from the manpage 'man 3 fi_cq_readerr'
                if (e.err == FI_ECANCELED)
                {
                    LF_DEB(NS_DEBUG::cnt_deb,
                        debug(debug::str<>("rxcq Cancelled"), "flags", debug::hex<6>(e.flags),
                            "len", debug::hex<6>(e.len), "context", NS_DEBUG::ptr(e.op_context)));
                    // the request was cancelled, we can simply exit
                    // as the canceller will have doone any cleanup needed
                    operation_context* handler = reinterpret_cast<operation_context*>(e.op_context);
                    handler->handle_cancelled();
                    return 0;
                }
                else if (e.err != FI_SUCCESS)
                {
                    NS_DEBUG::cnt_err.error(debug::str<>("poll_recv_queue"), "error code",
                        debug::dec<>(-e.err), "flags", debug::hex<6>(e.flags), "len",
                        debug::hex<6>(e.len), "context", NS_DEBUG::ptr(e.op_context), "error msg",
                        fi_cq_strerror(rx_cq, e.prov_errno, e.err_data, (char*)e.buf, e.len));
                }
                operation_context* handler = reinterpret_cast<operation_context*>(e.op_context);
                if (handler) handler->handle_error(e);
                return 0;
            }
        }
        //
        // release the lock and process each completion
        //
        if (ret > 0)
        {
            int processed = 0;
            for (int i = 0; i < ret; ++i)
            {
                ++recvs_complete;
                LF_DEB(NS_DEBUG::cnt_deb,
                    debug(debug::str<>("Completion"), i, "rxcq flags",
                        fi_tostr(&entry[i].flags, FI_TYPE_CQ_EVENT_FLAGS), "(",
                        debug::dec<>(entry[i].flags), ")", "context",
                        NS_DEBUG::ptr(entry[i].op_context), "length", debug::hex<6>(entry[i].len)));
                if ((entry[i].flags & (FI_TAGGED | FI_RECV)) != 0)
                {
                    LF_DEB(NS_DEBUG::cnt_deb,
                        debug(debug::str<>("Completion"), "rxcq tagged recv completion",
                            NS_DEBUG::ptr(entry[i].op_context)));

                    operation_context* handler =
                        reinterpret_cast<operation_context*>(entry[i].op_context);
                    processed += handler->handle_tagged_recv_completion(user_data);
                }
                else
                {
                    NS_DEBUG::cnt_err.error("Received an unknown rxcq completion",
                        debug::dec<>(entry[i].flags), debug::bin<64>(entry[i].flags));
                    std::terminate();
                }
            }
            return processed;
        }
        else if (ret == 0 || ret == -FI_EAGAIN)
        {
            // do nothing, we will try again on the next check
        }
        else { NS_DEBUG::cnt_err.error("unknown error in completion rxcq read"); }
        return 0;
    }

    // Jobs started using mpi don't have this info
    struct fi_info* set_src_dst_addresses(struct fi_info* info, bool tx)
    {
        (void)info; // unused variable warning
        (void)tx;   // unused variable warning

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("fi_dupinfo")));
        struct fi_info* hints = fi_dupinfo(info);
        if (!hints) throw NS_LIBFABRIC::fabric_error(0, "fi_dupinfo");
        // clear any Rx address data that might be set
        // free(hints->src_addr);
        // hints->src_addr = nullptr;
        // hints->src_addrlen = 0;
        free(hints->dest_addr);
        hints->dest_addr = nullptr;
        hints->dest_addrlen = 0;
        return hints;
    }
};

} // namespace oomph::libfabric
