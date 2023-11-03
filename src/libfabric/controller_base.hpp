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
#include <boost/lockfree/queue.hpp>
//
#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
//
#include "oomph_libfabric_defines.hpp"
//
#include "fabric_error.hpp"
#include "locality.hpp"
#include "memory_region.hpp"
#include "operation_context_base.hpp"

//#define DISABLE_FI_INJECT
//#define EXCESSIVE_POLLING_BACKOFF_MICRO_S 50

// ------------------------------------------------------------------

// ----------------------------------------
// auto progress (libfabric thread) or manual
// ----------------------------------------
static fi_progress
libfabric_progress_type()
{
    if (std::getenv("LIBFABRIC_AUTO_PROGRESS") == nullptr) return FI_PROGRESS_MANUAL;
    return FI_PROGRESS_AUTO;
}

static const char*
libfabric_progress_string()
{
    if (libfabric_progress_type() == FI_PROGRESS_AUTO) return "auto";
    return "manual";
}

// ----------------------------------------
// endpoint types
// We assume (to simplify things) that if you want a scalable Rx,
// you also have a scalable Tx
//
// Note that only GNI supports scalable endpoints currently
// Warning. It seems that scalable Rx contexts cannot be used when tagged or
// expected messages are used because the message is assigned to an rx context
// in a non deterministic way, so posting a tagged receive on context N might
// never complete as the tagged message when to context M, and appears as an
// unexpected message, completion on N never happens.
//
// When using unexpected mesages only, Rx contexts might be useful.
// ----------------------------------------
enum class endpoint_type : int
{
    single = 0,
    multiple = 1,
    threadlocalTx = 2,
    scalableTx = 3,
    scalableTxRx = 4,
};

// ----------------------------------------
// single endpoint or separate for send/recv
// ----------------------------------------
static endpoint_type
libfabric_endpoint_type()
{
    auto env_str = std::getenv("LIBFABRIC_ENDPOINT_TYPE");
    if (env_str == nullptr) return endpoint_type::single;
    if (std::string(env_str) == std::string("multiple") ||
        std::atoi(env_str) == int(endpoint_type::multiple))
        return endpoint_type::multiple;
    if (std::string(env_str) == std::string("threadlocal") ||
        std::atoi(env_str) == int(endpoint_type::threadlocalTx))
        return endpoint_type::threadlocalTx;
    if (std::string(env_str) == std::string("scalableTx") ||
        std::atoi(env_str) == int(endpoint_type::scalableTx))
        return endpoint_type::scalableTx;
    if (std::string(env_str) == std::string("scalableTxRx") ||
        std::atoi(env_str) == int(endpoint_type::scalableTxRx))
        return endpoint_type::scalableTxRx;
    // default is single endpoint type
    return endpoint_type::single;
}

static const char*
libfabric_endpoint_string()
{
    auto lf_ep_type = libfabric_endpoint_type();
    if (lf_ep_type == endpoint_type::multiple) return "multiple";
    if (lf_ep_type == endpoint_type::threadlocalTx) return "threadlocal";
    if (lf_ep_type == endpoint_type::scalableTx) return "scalableTx";
    if (lf_ep_type == endpoint_type::scalableTxRx) return "scalableTxRx";
    return "single";
}

// ----------------------------------------
// number of completions to handle per poll
// ----------------------------------------
static int
libfabric_completions_per_poll()
{
    auto env_str = std::getenv("LIBFABRIC_POLL_SIZE");
    if (env_str != nullptr)
    {
        try
        {
            return std::atoi(env_str);
        }
        catch (...)
        {
        }
    }
    return 4;
}

// ----------------------------------------
// Eager/Rendezvous threshold
// ----------------------------------------
static int
libfabric_rendezvous_threshold(int def_val)
{
    auto env_str = std::getenv("LIBFABRIC_RENDEZVOUS_THRESHOLD");
    if (env_str != nullptr)
    {
        try
        {
            char* end;
            return std::strtoul(env_str, &end, 0);
        }
        catch (...)
        {
        }
    }
    return def_val;
}

// ------------------------------------------------
// Needed on Cray for GNI extensions
// ------------------------------------------------
#ifdef HAVE_LIBFABRIC_GNI
#include "rdma/fi_ext_gni.h"
//#define OOMPH_GNI_REG "none"
#define OOMPH_GNI_REG "internal"
//#define OOMPH_GNI_REG "udreg"

static std::vector<std::pair<int, std::string>> gni_strs = {
    {GNI_MR_CACHE, "GNI_MR_CACHE"},
};

// clang-format off
static std::vector<std::pair<int, std::string>> gni_ints = {
    {GNI_MR_CACHE_LAZY_DEREG, "GNI_MR_CACHE_LAZY_DEREG"},
    {GNI_MR_HARD_REG_LIMIT, "GNI_MR_HARD_REG_LIMIT"},
    {GNI_MR_SOFT_REG_LIMIT, "GNI_MR_SOFT_REG_LIMIT"},
    {GNI_MR_HARD_STALE_REG_LIMIT, "GNI_MR_HARD_STALE_REG_LIMIT"},
    {GNI_MR_UDREG_REG_LIMIT, "GNI_MR_UDREG_REG_LIMIT"},
    {GNI_WAIT_THREAD_SLEEP, "GNI_WAIT_THREAD_SLEEP"},
    {GNI_DEFAULT_USER_REGISTRATION_LIMIT, "GNI_DEFAULT_USER_REGISTRATION_LIMIT"},
    {GNI_DEFAULT_PROV_REGISTRATION_LIMIT, "GNI_DEFAULT_PROV_REGISTRATION_LIMIT"},
    {GNI_WAIT_SHARED_MEMORY_TIMEOUT, "GNI_WAIT_SHARED_MEMORY_TIMEOUT"},
    {GNI_MSG_RENDEZVOUS_THRESHOLD, "GNI_MSG_RENDEZVOUS_THRESHOLD"},
    {GNI_RMA_RDMA_THRESHOLD, "GNI_RMA_RDMA_THRESHOLD"},
    {GNI_CONN_TABLE_INITIAL_SIZE, "GNI_CONN_TABLE_INITIAL_SIZE"},
    {GNI_CONN_TABLE_MAX_SIZE, "GNI_CONN_TABLE_MAX_SIZE"},
    {GNI_CONN_TABLE_STEP_SIZE, "GNI_CONN_TABLE_STEP_SIZE"},
    {GNI_VC_ID_TABLE_CAPACITY, "GNI_VC_ID_TABLE_CAPACITY"},
    {GNI_MBOX_PAGE_SIZE, "GNI_MBOX_PAGE_SIZE"},
    {GNI_MBOX_NUM_PER_SLAB, "GNI_MBOX_NUM_PER_SLAB"},
    {GNI_MBOX_MAX_CREDIT, "GNI_MBOX_MAX_CREDIT"},
    {GNI_MBOX_MSG_MAX_SIZE, "GNI_MBOX_MSG_MAX_SIZE"},
    {GNI_RX_CQ_SIZE, "GNI_RX_CQ_SIZE"},
    {GNI_TX_CQ_SIZE, "GNI_TX_CQ_SIZE"},
    {GNI_MAX_RETRANSMITS, "GNI_MAX_RETRANSMITS"},
    {GNI_XPMEM_ENABLE, "GNI_XPMEM_ENABLE"},
    {GNI_DGRAM_PROGRESS_TIMEOUT, "GNI_DGRAM_PROGRESS_TIMEOUT"}
};
// clang-format on
#endif

#define LIBFABRIC_FI_VERSION_MAJOR 1
#define LIBFABRIC_FI_VERSION_MINOR 15

namespace NS_DEBUG
{
// cppcheck-suppress ConfigurationNotChecked
static NS_DEBUG::enable_print<true> cnb_deb("CONBASE");
static NS_DEBUG::enable_print<true> cnb_err("CONBASE");
} // namespace NS_DEBUG

/** @brief a class to return the number of progressed callbacks */
struct progress_status
{
    int m_num_sends = 0;
    int m_num_recvs = 0;

    int num() const noexcept { return m_num_sends + m_num_recvs; }
    int num_sends() const noexcept { return m_num_sends; }
    int num_recvs() const noexcept { return m_num_recvs; }

    progress_status& operator+=(const progress_status& other) noexcept
    {
        m_num_sends += other.m_num_sends;
        m_num_recvs += other.m_num_recvs;
        return *this;
    }
};

namespace NS_LIBFABRIC
{
/// A wrapper around fi_close that reports any error
/// Because we use so many handles, we must be careful to
/// delete them all before closing resources that use them
template<typename Handle>
void
fidclose(Handle fid, const char* msg)
{
    LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("closing"), msg));
    int ret = fi_close(fid);
    if (ret == -FI_EBUSY) { throw NS_LIBFABRIC::fabric_error(ret, "fi_close EBUSY"); }
    else if (ret == FI_SUCCESS) { return; }
    throw NS_LIBFABRIC::fabric_error(ret, "fi_close error");
}

/// when using thread local endpoints, we encapsulate things that
/// are needed to manage an endpoint
struct endpoint_wrapper
{
  private:
    friend class controller;

    fid_ep*     ep_ = nullptr;
    fid_cq*     rq_ = nullptr;
    fid_cq*     tq_ = nullptr;
    const char* name_ = nullptr;

  public:
    endpoint_wrapper() {}
    endpoint_wrapper(fid_ep* ep, fid_cq* rq, fid_cq* tq, const char* name)
    : ep_(ep)
    , rq_(rq)
    , tq_(tq)
    , name_(name)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__, name_);
    }

    // to keep boost::lockfree happy, we need these copy operators
    endpoint_wrapper(const endpoint_wrapper& ep) = default;
    endpoint_wrapper& operator=(const endpoint_wrapper& ep) = default;

    void cleanup()
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__, name_);
        if (ep_)
        {
            fidclose(&ep_->fid, "endpoint");
            ep_ = nullptr;
        }
        if (rq_)
        {
            fidclose(&rq_->fid, "rq");
            rq_ = nullptr;
        }
        if (tq_)
        {
            fidclose(&tq_->fid, "tq");
            tq_ = nullptr;
        }
    }

    inline fid_ep*     get_ep() { return ep_; }
    inline fid_cq*     get_rx_cq() { return rq_; }
    inline fid_cq*     get_tx_cq() { return tq_; }
    inline void        set_tx_cq(fid_cq* cq) { tq_ = cq; }
    inline const char* get_name() { return name_; }
};

using region_type = NS_MEMORY::memory_handle;
using endpoint_context_pool =
    boost::lockfree::queue<endpoint_wrapper, boost::lockfree::fixed_sized<false>>;

struct stack_endpoint
{
    endpoint_wrapper       endpoint_;
    endpoint_context_pool* pool_;
    //
    stack_endpoint()
    : endpoint_()
    , pool_(nullptr)
    {
    }
    //
    stack_endpoint(fid_ep* ep, fid_cq* rq, fid_cq* tq, const char* name,
        endpoint_context_pool* pool)
    : endpoint_(ep, rq, tq, name)
    , pool_(pool)
    {
    }
    //
    stack_endpoint& operator=(stack_endpoint&& other)
    {
        endpoint_ = std::move(other.endpoint_);
        pool_ = std::exchange(other.pool_, nullptr);
        return *this;
    }

    ~stack_endpoint()
    {
        if (!pool_) return;
        LF_DEB(NS_DEBUG::cnb_deb,
            trace(debug::str<>("Scalable Ep"), "used push", "ep", NS_DEBUG::ptr(get_ep()), "tx cq",
                NS_DEBUG::ptr(get_tx_cq()), "rx cq", NS_DEBUG::ptr(get_rx_cq())));
        pool_->push(endpoint_);
    }

    inline fid_ep* get_ep() { return endpoint_.get_ep(); }

    inline fid_cq* get_rx_cq() { return endpoint_.get_rx_cq(); }

    inline fid_cq* get_tx_cq() { return endpoint_.get_tx_cq(); }
};

struct endpoints_lifetime_manager
{
    // threadlocal endpoints
    static inline thread_local stack_endpoint tl_tx_;
    static inline thread_local stack_endpoint tl_stx_;
    static inline thread_local stack_endpoint tl_srx_;
    // non threadlocal endpoints, tx/rx
    endpoint_wrapper ep_tx_;
    endpoint_wrapper ep_rx_;
};

template<typename Derived>
class controller_base
{
  public:
    typedef std::mutex                   mutex_type;
    typedef std::lock_guard<mutex_type>  scoped_lock;
    typedef std::unique_lock<mutex_type> unique_lock;

  protected:
    // For threadlocal/scalable endpoints,
    // we use a dedicated threadlocal endpoint wrapper
    std::unique_ptr<endpoints_lifetime_manager> eps_;

    using endpoint_context_pool =
        boost::lockfree::queue<endpoint_wrapper, boost::lockfree::fixed_sized<false>>;
    endpoint_context_pool tx_endpoints_;
    endpoint_context_pool rx_endpoints_;

    struct fi_info*    fabric_info_;
    struct fid_fabric* fabric_;
    struct fid_domain* fabric_domain_;
    struct fid_pep*    ep_passive_;

    struct fid_av* av_;
    endpoint_type  endpoint_type_;

    locality here_;
    locality root_;

    // used during queue creation setup and during polling
    mutex_type controller_mutex_;

    // used to protect send/recv resources
    alignas(64) mutex_type send_mutex_;
    alignas(64) mutex_type recv_mutex_;

    std::size_t tx_inject_size_;
    std::size_t tx_attr_size_;
    std::size_t rx_attr_size_;

    uint32_t max_completions_per_poll_;
    uint32_t msg_rendezvous_threshold_;
    inline static constexpr uint32_t max_completions_array_limit_ = 256;

    static inline thread_local std::chrono::steady_clock::time_point send_poll_stamp;
    static inline thread_local std::chrono::steady_clock::time_point recv_poll_stamp;

  public:
    NS_LIBFABRIC::simple_counter<int, false> sends_posted_;
    NS_LIBFABRIC::simple_counter<int, false> recvs_posted_;
    NS_LIBFABRIC::simple_counter<int, false> sends_readied_;
    NS_LIBFABRIC::simple_counter<int, false> recvs_readied_;
    NS_LIBFABRIC::simple_counter<int, false> sends_complete;
    NS_LIBFABRIC::simple_counter<int, false> recvs_complete;

    void finvoke(const char* msg, const char* err, int ret)
    {
        LF_DEB(NS_DEBUG::cnb_deb, trace(debug::str<>(msg)));
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, err);
    }

  public:
    // --------------------------------------------------------------------
    controller_base()
    : eps_(nullptr)
    , tx_endpoints_(1)
    , rx_endpoints_(1)
    , fabric_info_(nullptr)
    , fabric_(nullptr)
    , fabric_domain_(nullptr)
    , ep_passive_(nullptr)
    , av_(nullptr)
    , tx_inject_size_(0)
    , tx_attr_size_(0)
    , rx_attr_size_(0)
    , max_completions_per_poll_(1)
    , msg_rendezvous_threshold_(0x4000)
    , sends_posted_(0)
    , recvs_posted_(0)
    , sends_readied_(0)
    , recvs_readied_(0)
    , sends_complete(0)
    , recvs_complete(0)
    {
    }

    // --------------------------------------------------------------------
    // clean up all resources
    ~controller_base()
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);
        unsigned int          messages_handled_ = 0;
        unsigned int          rma_reads_ = 0;
        unsigned int          recv_deletes_ = 0;

        LF_DEB(NS_DEBUG::cnb_deb,
            debug(debug::str<>("counters"), "Received messages", debug::dec<>(messages_handled_),
                "Total reads", debug::dec<>(rma_reads_), "Total deletes",
                debug::dec<>(recv_deletes_), "deletes error",
                debug::dec<>(messages_handled_ - recv_deletes_)));

        tx_endpoints_.consume_all([](endpoint_wrapper& ep) { ep.cleanup(); });
        rx_endpoints_.consume_all([](endpoint_wrapper& ep) { ep.cleanup(); });

        // No cleanup threadlocals : done by consume_all cleanup above
        // eps_->tl_tx_.endpoint_.cleanup();
        // eps_->tl_stx_.endpoint_.cleanup();
        // eps_->tl_srx_.endpoint_.cleanup();

        // non threadlocal endpoints, tx/rx
        eps_->ep_tx_.cleanup();
        eps_->ep_rx_.cleanup();

        // Cleanup endpoints
        eps_.reset(nullptr);

        // delete adddress vector
        fidclose(&av_->fid, "Address Vector");

        try
        {
            fidclose(&fabric_domain_->fid, "Domain");
        }
        catch (fabric_error& e)
        {
            std::cout << "fabric domain close failed : Ensure all RMA "
                         "objects are freed before program termination"
                      << std::endl;
        }
        fidclose(&fabric_->fid, "Fabric");

        // clean up
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("freeing fabric_info")));

        fi_freeinfo(fabric_info_);
    }

    // --------------------------------------------------------------------
    // setup an endpoint for receiving messages,
    // usually an rx endpoint is shared by all threads
    endpoint_wrapper create_rx_endpoint(struct fid_domain* domain, struct fi_info* info,
        struct fid_av* av)
    {
        auto ep_rx = new_endpoint_active(domain, info, false);

        // bind address vector
        bind_address_vector_to_endpoint(ep_rx, av);

        // create a completion queue for the rx endpoint
        info->rx_attr->op_flags |= FI_COMPLETION;
        auto rx_cq = create_completion_queue(domain, info->rx_attr->size, "rx");

        // bind CQ to endpoint
        bind_queue_to_endpoint(ep_rx, rx_cq, FI_RECV, "rx");
        return endpoint_wrapper(ep_rx, rx_cq, nullptr, "rx");
    }

    // --------------------------------------------------------------------
    // initialize the basic fabric/domain/name
    template<typename... Args>
    void initialize(std::string const& provider, bool rootnode, int size, size_t threads,
        Args&&... args)
    {
        LF_DEB(NS_DEBUG::cnb_deb, eval([]() { std::cout.setf(std::ios::unitbuf); }));
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        max_completions_per_poll_ = libfabric_completions_per_poll();
        LF_DEB(NS_DEBUG::cnb_err,
            debug(debug::str<>("Poll completions"), debug::dec<3>(max_completions_per_poll_)));

        uint32_t default_val = (threads == 1) ? 0x400 : 0x4000;
        msg_rendezvous_threshold_ = libfabric_rendezvous_threshold(default_val);
        LF_DEB(NS_DEBUG::cnb_err,
            debug(debug::str<>("Rendezvous threshold"), debug::hex<4>(msg_rendezvous_threshold_)));

        endpoint_type_ = static_cast<endpoint_type>(libfabric_endpoint_type());
        LF_DEB(NS_DEBUG::cnb_err, debug(debug::str<>("Endpoints"), libfabric_endpoint_string()));

        eps_ = std::make_unique<endpoints_lifetime_manager>();

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Threads"), debug::dec<3>(threads)));

        open_fabric(provider, threads, rootnode);

        // create an address vector that will be bound to (all) endpoints
        av_ = create_address_vector(fabric_info_, size, threads);

        // we need an rx endpoint in all cases except scalable rx
        if (endpoint_type_ != endpoint_type::scalableTxRx)
        {
            // setup an endpoint for receiving messages
            // rx endpoint is typically shared by all threads
            eps_->ep_rx_ = create_rx_endpoint(fabric_domain_, fabric_info_, av_);
        }

        if (endpoint_type_ == endpoint_type::single)
        {
            // always bind a tx cq to the rx endpoint for single endpoint type
            auto tx_cq = bind_tx_queue_to_rx_endpoint(fabric_info_, eps_->ep_rx_.get_ep());
            eps_->ep_rx_.set_tx_cq(tx_cq);
        }
        else if (endpoint_type_ != endpoint_type::scalableTxRx)
        {
#if defined(HAVE_LIBFABRIC_SOCKETS) || defined(HAVE_LIBFABRIC_TCP) ||                              \
    defined(HAVE_LIBFABRIC_VERBS) || defined(HAVE_LIBFABRIC_CXI) || defined(HAVE_LIBFABRIC_EFA)
            // it appears that the rx endpoint cannot be enabled if it does not
            // have a Tx CQ (at least when using sockets), so we create a dummy
            // Tx CQ and bind it just to stop libfabric from triggering an error.
            // The tx_cq won't actually be used because the user will get the real
            // tx endpoint which will have the correct cq bound to it
            auto dummy_cq = bind_tx_queue_to_rx_endpoint(fabric_info_, eps_->ep_rx_.get_ep());
            eps_->ep_rx_.set_tx_cq(dummy_cq);
#endif
        }

        if (endpoint_type_ == endpoint_type::multiple)
        {
            // create a separate Tx endpoint for sending messages
            // note that the CQ needs FI_RECV even though its a Tx cq to keep
            // some providers happy as they trigger an error if an endpoint
            // has no Rx cq attached (appears to be a progress related bug)
            auto ep_tx = new_endpoint_active(fabric_domain_, fabric_info_, true);

            // create a completion queue for tx endpoint
            fabric_info_->tx_attr->op_flags |= (FI_INJECT_COMPLETE | FI_COMPLETION);
            auto tx_cq =
                create_completion_queue(fabric_domain_, fabric_info_->tx_attr->size, "tx multiple");

            bind_queue_to_endpoint(ep_tx, tx_cq, FI_TRANSMIT | FI_RECV, "tx multiple");
            bind_address_vector_to_endpoint(ep_tx, av_);
            enable_endpoint(ep_tx, "tx multiple");

            // combine endpoints and CQ into wrapper for convenience
            eps_->ep_tx_ = endpoint_wrapper(ep_tx, nullptr, tx_cq, "tx multiple");
        }
        else if (endpoint_type_ == endpoint_type::threadlocalTx)
        {
            // each thread creates a Tx endpoint on first call to get_tx_endpoint()
        }
        else if (endpoint_type_ == endpoint_type::scalableTx ||
                 endpoint_type_ == endpoint_type::scalableTxRx)
        {
            // setup tx contexts for each possible thread
            size_t threads_allocated = 0;
            auto   ep_sx = new_endpoint_scalable(fabric_domain_, fabric_info_, true /*Tx*/, threads,
                  threads_allocated);

            LF_DEB(NS_DEBUG::cnb_deb, trace(debug::str<>("scalable endpoint ok"),
                                          "Contexts allocated", debug::dec<4>(threads_allocated)));

            finvoke("fi_scalable_ep_bind AV", "fi_scalable_ep_bind",
                fi_scalable_ep_bind(ep_sx, &av_->fid, 0));

            // prepare the stack for insertions
            tx_endpoints_.reserve(threads_allocated);
            //
            for (unsigned int i = 0; i < threads_allocated; i++)
            {
                [[maybe_unused]] auto scp =
                    NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), "scalable", debug::dec<4>(i));

                // For threadlocal/scalable endpoints, tx/rx resources
                fid_ep* scalable_ep_tx;
                fid_cq* scalable_cq_tx;

                // Create a Tx context, cq, bind and enable
                finvoke("create tx context", "fi_tx_context",
                    fi_tx_context(ep_sx, i, NULL, &scalable_ep_tx, NULL));
                scalable_cq_tx = create_completion_queue(fabric_domain_,
                    fabric_info_->tx_attr->size, "tx scalable");
                bind_queue_to_endpoint(scalable_ep_tx, scalable_cq_tx, FI_TRANSMIT, "tx scalable");
                enable_endpoint(scalable_ep_tx, "tx scalable");

                endpoint_wrapper tx(scalable_ep_tx, nullptr, scalable_cq_tx, "tx scalable");
                LF_DEB(NS_DEBUG::cnb_deb,
                    trace(debug::str<>("Scalable Ep"), "initial tx push", "ep",
                        NS_DEBUG::ptr(tx.get_ep()), "tx cq", NS_DEBUG::ptr(tx.get_tx_cq()), "rx cq",
                        NS_DEBUG::ptr(tx.get_rx_cq())));
                tx_endpoints_.push(tx);
            }

            eps_->ep_tx_ = endpoint_wrapper(ep_sx, nullptr, nullptr, "rx scalable");
        }

        // once enabled we can get the address
        enable_endpoint(eps_->ep_rx_.get_ep(), "rx here");
        here_ = get_endpoint_address(&eps_->ep_rx_.get_ep()->fid);
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("setting 'here'"), iplocality(here_)));

        //        // if we are using scalable endpoints, then setup tx/rx contexts
        //        // we will us a single endpoint for all Tx/Rx contexts
        //        if (endpoint_type_ == endpoint_type::scalableTx ||
        //            endpoint_type_ == endpoint_type::scalableTxRx)
        //        {

        //            // thread slots might not be same as what we asked for
        //            size_t threads_allocated = 0;
        //            auto   ep_sx = new_endpoint_scalable(fabric_domain_, fabric_info_, true /*Tx*/, threads,
        //                  threads_allocated);
        //            if (!ep_sx)
        //                throw NS_LIBFABRIC::fabric_error(FI_EOTHER, "fi_scalable endpoint creation failed");

        //            LF_DEB(NS_DEBUG::cnb_deb, trace(debug::str<>("scalable endpoint ok"),
        //                                         "Contexts allocated", debug::dec<4>(threads_allocated)));

        //            // prepare the stack for insertions
        //            tx_endpoints_.reserve(threads_allocated);
        //            rx_endpoints_.reserve(threads_allocated);
        //            //
        //            for (unsigned int i = 0; i < threads_allocated; i++)
        //            {
        //                [[maybe_unused]] auto scp =
        //                    NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), "scalable", debug::dec<4>(i));

        //                // For threadlocal/scalable endpoints, tx/rx resources
        //                fid_ep* scalable_ep_tx;
        //                fid_cq* scalable_cq_tx;
        ////                fid_ep* scalable_ep_rx;
        ////                fid_cq* scalable_cq_rx;

        //                // Tx context setup
        //                finvoke("create tx context", "fi_tx_context",
        //                    fi_tx_context(ep_sx, i, NULL, &scalable_ep_tx, NULL));

        //                scalable_cq_tx = create_completion_queue(fabric_domain_,
        //                    fabric_info_->tx_attr->size, "tx scalable");

        //                bind_queue_to_endpoint(scalable_ep_tx, scalable_cq_tx, FI_TRANSMIT, "tx scalable");

        //                enable_endpoint(scalable_ep_tx, "tx scalable");

        //                endpoint_wrapper tx(scalable_ep_tx, nullptr, scalable_cq_tx, "tx scalable");
        //                LF_DEB(NS_DEBUG::cnb_deb,
        //                    trace(debug::str<>("Scalable Ep"), "initial tx push", "ep",
        //                        NS_DEBUG::ptr(tx.get_ep()), "tx cq", NS_DEBUG::ptr(tx.get_tx_cq()), "rx cq",
        //                        NS_DEBUG::ptr(tx.get_rx_cq())));
        //                tx_endpoints_.push(tx);

        //                // Rx contexts
        ////                finvoke("create rx context", "fi_rx_context",
        ////                    fi_rx_context(ep_sx, i, NULL, &scalable_ep_rx, NULL));

        ////                scalable_cq_rx =
        ////                    create_completion_queue(fabric_domain_, fabric_info_->rx_attr->size, "rx");

        ////                bind_queue_to_endpoint(scalable_ep_rx, scalable_cq_rx, FI_RECV, "rx scalable");

        ////                enable_endpoint(scalable_ep_rx, "rx scalable");

        ////                endpoint_wrapper rx(scalable_ep_rx, scalable_cq_rx, nullptr, "rx scalable");
        ////                LF_DEB(NS_DEBUG::cnb_deb,
        ////                    trace(debug::str<>("Scalable Ep"), "initial rx push", "ep",
        ////                        NS_DEBUG::ptr(rx.get_ep()), "tx cq", NS_DEBUG::ptr(rx.get_tx_cq()), "rx cq",
        ////                        NS_DEBUG::ptr(rx.get_rx_cq())));
        ////                rx_endpoints_.push(rx);
        //            }

        //            finvoke("fi_scalable_ep_bind AV", "fi_scalable_ep_bind",
        //                fi_scalable_ep_bind(ep_sx, &av_->fid, 0));

        //            eps_->ep_tx_ = endpoint_wrapper(ep_sx, nullptr, nullptr, "rx scalable");

        return static_cast<Derived*>(this)->initialize_derived(provider, rootnode, size, threads,
            std::forward<Args>(args)...);
    }

    // --------------------------------------------------------------------
    constexpr uint64_t caps_flags() { return static_cast<Derived*>(this)->caps_flags(); }

    // --------------------------------------------------------------------
    constexpr fi_threading threadlevel_flags()
    {
        return static_cast<Derived*>(this)->threadlevel_flags();
    }

    // --------------------------------------------------------------------
    constexpr int memory_registration_mode_flags()
    {
        // use basic registration for providers except CXI
#if defined(HAVE_LIBFABRIC_CXI)
        int base_flags =
            FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY | FI_MR_LOCAL | FI_MR_MMU_NOTIFY;
        return base_flags | FI_MR_ENDPOINT | FI_MR_HMEM;
#elif defined(HAVE_LIBFABRIC_GNI)
        return FI_MR_BASIC; // FI_MR_SCALABLE one day?;
#elif defined(HAVE_LIBFABRIC_EFA)
        return FI_MR_LOCAL;
        int base_flags =
            FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY | FI_MR_LOCAL | FI_MR_MMU_NOTIFY;
        return base_flags | FI_MR_HMEM;
#else
        return FI_MR_BASIC;
#endif
    }

    // --------------------------------------------------------------------
    uint32_t rendezvous_threshold() { return msg_rendezvous_threshold_; }
    // --------------------------------------------------------------------
    // initialize the basic fabric/domain/name
    void open_fabric(std::string const& provider, int threads, bool rootnode)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        struct fi_info* fabric_hints_ = fi_allocinfo();
        if (!fabric_hints_)
        {
            throw NS_LIBFABRIC::fabric_error(-1, "Failed to allocate fabric hints");
        }

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Here locality"), iplocality(here_)));

#if defined(HAVE_LIBFABRIC_SOCKETS) || defined(HAVE_LIBFABRIC_TCP) || defined(HAVE_LIBFABRIC_VERBS)
        fabric_hints_->addr_format = FI_SOCKADDR_IN;
#elif defined(HAVE_LIBFABRIC_EFA)
        fabric_hints_->addr_format = FI_ADDR_EFA;
#endif

        fabric_hints_->caps = caps_flags();

        fabric_hints_->mode = FI_CONTEXT /*| FI_MR_LOCAL*/;
        if (provider.c_str() == std::string("tcp"))
        {
            fabric_hints_->fabric_attr->prov_name =
                strdup(std::string(provider + ";ofi_rxm").c_str());
        }
        else if (provider.c_str() == std::string("verbs"))
        {
            fabric_hints_->fabric_attr->prov_name =
                strdup(std::string(provider + ";ofi_rxm").c_str());
        }
        else { fabric_hints_->fabric_attr->prov_name = strdup(provider.c_str()); }
        LF_DEB(NS_DEBUG::cnb_deb,
            debug(debug::str<>("fabric provider"), fabric_hints_->fabric_attr->prov_name));

        fabric_hints_->domain_attr->mr_mode = memory_registration_mode_flags();

        // Enable/Disable the use of progress threads
        auto progress = libfabric_progress_type();
        fabric_hints_->domain_attr->control_progress = progress;
        fabric_hints_->domain_attr->data_progress = progress;
        LF_DEB(NS_DEBUG::cnb_err, debug(debug::str<>("progress"), libfabric_progress_string()));

        if (threads > 1)
        {
            LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("FI_THREAD_FID")));
            // Enable thread safe mode (Does not work with psm2 provider)
            // fabric_hints_->domain_attr->threading = FI_THREAD_SAFE;
            //fabric_hints_->domain_attr->threading = FI_THREAD_FID;
            fabric_hints_->domain_attr->threading = threadlevel_flags();
        }
        else
        {
            LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("FI_THREAD_DOMAIN")));
            // we serialize everything
            fabric_hints_->domain_attr->threading = FI_THREAD_DOMAIN;
        }

        // Enable resource management
        fabric_hints_->domain_attr->resource_mgmt = FI_RM_ENABLED;

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("fabric endpoint"), "RDM"));
        fabric_hints_->ep_attr->type = FI_EP_RDM;

        uint64_t flags = 0;
        LF_DEB(NS_DEBUG::cnb_deb,
            debug(debug::str<>("get fabric info"), "FI_VERSION",
                debug::dec(LIBFABRIC_FI_VERSION_MAJOR), debug::dec(LIBFABRIC_FI_VERSION_MINOR)));

        int ret = fi_getinfo(FI_VERSION(LIBFABRIC_FI_VERSION_MAJOR, LIBFABRIC_FI_VERSION_MINOR),
            nullptr, nullptr, flags, fabric_hints_, &fabric_info_);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "Failed to get fabric info");

        if (rootnode)
        {
            LF_DEB(NS_DEBUG::cnb_err,
                trace(debug::str<>("Fabric info"), "\n", fi_tostr(fabric_info_, FI_TYPE_INFO)));
        }

        bool context = (fabric_hints_->mode & FI_CONTEXT) != 0;
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Requires FI_CONTEXT"), context));

        bool mrlocal = (fabric_hints_->domain_attr->mr_mode & FI_MR_LOCAL) != 0;
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Requires FI_MR_LOCAL"), mrlocal));

        bool mrbind = (fabric_hints_->domain_attr->mr_mode & FI_MR_ENDPOINT) != 0;
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Requires FI_MR_ENDPOINT"), mrbind));

        /* Check if provider requires heterogeneous memory registration */
        bool mrhmem = (fabric_hints_->domain_attr->mr_mode & FI_MR_HMEM) != 0;
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Requires FI_MR_HMEM"), mrhmem));

        bool mrhalloc = (fabric_hints_->domain_attr->mr_mode & FI_MR_ALLOCATED) != 0;
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Requires FI_MR_ALLOCATED"), mrhalloc));

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Creating fi_fabric")));
        ret = fi_fabric(fabric_info_->fabric_attr, &fabric_, nullptr);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "Failed to get fi_fabric");

        // Allocate a domain.
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Allocating domain")));
        ret = fi_domain(fabric_, fabric_info_, &fabric_domain_, nullptr);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "fi_domain");

#if defined(HAVE_LIBFABRIC_GNI)
        {
            [[maybe_unused]] auto scp =
                NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), "GNI memory registration block");

            LF_DEB(NS_DEBUG::cnb_err, debug(debug::str<>("-------"), "GNI String values"));
            // Dump out all vars for debug purposes
            for (auto& gni_data : gni_strs)
            {
                _set_check_domain_op_value<const char*>(gni_data.first, 0, gni_data.second.c_str(),
                    false);
            }
            LF_DEB(NS_DEBUG::cnb_err, debug(debug::str<>("-------"), "GNI Int values"));
            for (auto& gni_data : gni_ints)
            {
                _set_check_domain_op_value<uint32_t>(gni_data.first, 0, gni_data.second.c_str(),
                    false);
            }
            LF_DEB(NS_DEBUG::cnb_err, debug(debug::str<>("-------")));

            // --------------------------
            // GNI_MR_CACHE
            // set GNI mem reg to be either none, internal or udreg
            //
            _set_check_domain_op_value<char*>(GNI_MR_CACHE, const_cast<char*>(OOMPH_GNI_REG),
                "GNI_MR_CACHE");

            // --------------------------
            // GNI_MR_UDREG_REG_LIMIT
            // Experiments showed default value of 2048 too high if
            // launching multiple clients on one node
            //
            int32_t udreg_limit = 0x0800; // 0x0400 = 1024, 0x0800 = 2048
            _set_check_domain_op_value<int32_t>(GNI_MR_UDREG_REG_LIMIT, udreg_limit,
                "GNI_MR_UDREG_REG_LIMIT");

            // --------------------------
            // GNI_MR_CACHE_LAZY_DEREG
            // Enable lazy deregistration in MR cache
            //
            int32_t enable = 1;
            LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("setting GNI_MR_CACHE_LAZY_DEREG")));
            _set_check_domain_op_value<int32_t>(GNI_MR_CACHE_LAZY_DEREG, enable,
                "GNI_MR_CACHE_LAZY_DEREG");

            // --------------------------
            // GNI_MSG_RENDEZVOUS_THRESHOLD (c.f. GNI_RMA_RDMA_THRESHOLD)
            //
            int32_t thresh = msg_rendezvous_threshold_;
            _set_check_domain_op_value<int32_t>(GNI_MSG_RENDEZVOUS_THRESHOLD, thresh,
                "GNI_MSG_RENDEZVOUS_THRESHOLD");
        }
#endif
        tx_inject_size_ = fabric_info_->tx_attr->inject_size;

        // the number of preposted receives, and sender queue depth
        // is set by querying the tx/tx attr sizes
        tx_attr_size_ = std::min(size_t(512), fabric_info_->tx_attr->size / 2);
        rx_attr_size_ = std::min(size_t(512), fabric_info_->rx_attr->size / 2);
        fi_freeinfo(fabric_hints_);
    }

    // --------------------------------------------------------------------
    struct fi_info* set_src_dst_addresses(struct fi_info* info, bool tx)
    {
        return static_cast<Derived*>(this)->set_src_dst_addresses(info, tx);
    }

#ifdef HAVE_LIBFABRIC_GNI
    // --------------------------------------------------------------------
    // Special GNI extensions to disable memory registration cache

    // if set is false, the old value is returned and nothing is set
    template<typename T>
    int _set_check_domain_op_value(int op, T value, const char* info, bool set = true)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);
        static struct fi_gni_ops_domain* gni_domain_ops = nullptr;
        int                              ret = 0;

        if (gni_domain_ops == nullptr)
        {
            ret = fi_open_ops(&fabric_domain_->fid, FI_GNI_DOMAIN_OPS_1, 0, (void**)&gni_domain_ops,
                nullptr);
            LF_DEB(NS_DEBUG::cnb_deb,
                debug(debug::str<>("gni open ops"), (ret == 0 ? "OK" : "FAIL"),
                    NS_DEBUG::ptr(gni_domain_ops)));
        }

        // if open was ok and set flag is present, then set value
        if (ret == 0 && set)
        {
            ret = gni_domain_ops->set_val(&fabric_domain_->fid, (dom_ops_val_t)(op),
                reinterpret_cast<void*>(&value));

            LF_DEB(NS_DEBUG::cnb_deb,
                debug(debug::str<>("gni set ops val"), value, (ret == 0 ? "OK" : "FAIL")));
        }

        // Get the value (so we can check that the value we set is now returned)
        T new_value;
        ret = gni_domain_ops->get_val(&fabric_domain_->fid, (dom_ops_val_t)(op), &new_value);
        if constexpr (std::is_integral<T>::value)
        {
            LF_DEB(NS_DEBUG::cnb_err, debug(debug::str<>("gni op val"), (ret == 0 ? "OK" : "FAIL"),
                                          info, debug::hex<8>(new_value)));
        }
        else
        {
            LF_DEB(NS_DEBUG::cnb_err,
                debug(debug::str<>("gni op val"), (ret == 0 ? "OK" : "FAIL"), info, new_value));
        }
        //
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, std::string("setting ") + info);

        return ret;
    }
#endif

    // --------------------------------------------------------------------
    struct fid_ep* new_endpoint_active(struct fid_domain* domain, struct fi_info* info, bool tx)
    {
        // don't allow multiple threads to call endpoint create at the same time
        scoped_lock lock(controller_mutex_);

        // make sure src_addr/dst_addr are set accordingly
        // and we do not create two endpoint with the same src address
        struct fi_info* hints = set_src_dst_addresses(info, tx);

        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);
        LF_DEB(NS_DEBUG::cnb_deb,
            debug(debug::str<>("Got info mode"), (info->mode & FI_NOTIFY_FLAGS_ONLY)));

        struct fid_ep* ep;
        int            ret = fi_endpoint(domain, hints, &ep, nullptr);
        if (ret)
        {
            throw NS_LIBFABRIC::fabric_error(ret, "fi_endpoint (too many threadlocal "
                                                  "endpoints?)");
        }
        fi_freeinfo(hints);
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("new_endpoint_active"), NS_DEBUG::ptr(ep)));
        return ep;
    }

    // --------------------------------------------------------------------
    struct fid_ep* new_endpoint_scalable(struct fid_domain* domain, struct fi_info* info, bool tx,
        size_t threads, size_t& threads_allocated)
    {
        // don't allow multiple threads to call endpoint create at the same time
        scoped_lock lock(controller_mutex_);

        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("fi_dupinfo")));
        struct fi_info* hints = fi_dupinfo(info);
        if (!hints) throw NS_LIBFABRIC::fabric_error(0, "fi_dupinfo");

        int             flags = 0;
        struct fi_info* new_hints = nullptr;
        int ret = fi_getinfo(FI_VERSION(LIBFABRIC_FI_VERSION_MAJOR, LIBFABRIC_FI_VERSION_MINOR),
            nullptr, nullptr, flags, hints, &new_hints);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "fi_getinfo");

        // Check the optimal number of TX/RX contexts supported by the provider
        size_t context_count = 0;
        if (tx) { context_count = std::min(new_hints->domain_attr->tx_ctx_cnt, threads); }
        else { context_count = std::min(new_hints->domain_attr->rx_ctx_cnt, threads); }

        // clang-format off
        LF_DEB(NS_DEBUG::cnb_deb,
            trace(debug::str<>("scalable endpoint"),
                  "Tx", tx,
                  "Threads", debug::dec<3>(threads),
                  "tx_ctx_cnt", debug::dec<3>(new_hints->domain_attr->tx_ctx_cnt),
                  "rx_ctx_cnt", debug::dec<3>(new_hints->domain_attr->rx_ctx_cnt),
                  "context_count", debug::dec<3>(context_count)));
        // clang-format on

        threads_allocated = context_count;
        new_hints->ep_attr->tx_ctx_cnt = context_count;
        new_hints->ep_attr->rx_ctx_cnt = context_count;

        struct fid_ep* ep;
        ret = fi_scalable_ep(domain, new_hints, &ep, nullptr);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "fi_scalable_ep");
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("new_endpoint_scalable"), NS_DEBUG::ptr(ep)));
        fi_freeinfo(hints);
        return ep;
    }

    // --------------------------------------------------------------------
    endpoint_wrapper& get_rx_endpoint()
    {
        static auto rx = NS_DEBUG::cnb_deb.make_timer(1, debug::str<>("get_rx_endpoint"));
        LF_DEB(NS_DEBUG::cnb_deb, timed(rx));

        if (endpoint_type_ == endpoint_type::scalableTxRx)
        {
            if (eps_->tl_srx_.get_ep() == nullptr)
            {
                endpoint_wrapper ep;
                bool             ok = rx_endpoints_.pop(ep);
                if (!ok)
                {
                    // clang-format off
                    LF_DEB(NS_DEBUG::cnb_deb, error(debug::str<>("Scalable Ep"), "pop rx",
                        "ep", NS_DEBUG::ptr(ep.get_ep()),
                        "tx cq", NS_DEBUG::ptr(ep.get_tx_cq()),
                        "rx cq", NS_DEBUG::ptr(ep.get_rx_cq())));
                    // clang-format on
                    throw std::runtime_error("rx endpoint wrapper pop fail");
                }
                eps_->tl_srx_ = stack_endpoint(ep.get_ep(), ep.get_rx_cq(), ep.get_tx_cq(),
                    ep.get_name(), &rx_endpoints_);
                LF_DEB(NS_DEBUG::cnb_deb, trace(debug::str<>("Scalable Ep"), "pop rx", "ep",
                                              NS_DEBUG::ptr(eps_->tl_srx_.get_ep()), "tx cq",
                                              NS_DEBUG::ptr(eps_->tl_srx_.get_tx_cq()), "rx cq",
                                              NS_DEBUG::ptr(eps_->tl_srx_.get_rx_cq())));
            }
            return eps_->tl_srx_.endpoint_;
        }
        // otherwise just return the normal Rx endpoint
        return eps_->ep_rx_;
    }

    // --------------------------------------------------------------------
    endpoint_wrapper& get_tx_endpoint()
    {
        if (endpoint_type_ == endpoint_type::threadlocalTx)
        {
            if (eps_->tl_tx_.get_ep() == nullptr)
            {
                [[maybe_unused]] auto scp =
                    NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__, "threadlocal");

                // create a completion queue for tx endpoint
                fabric_info_->tx_attr->op_flags |= (FI_INJECT_COMPLETE | FI_COMPLETION);
                auto tx_cq = create_completion_queue(fabric_domain_, fabric_info_->tx_attr->size,
                    "tx threadlocal");

                // setup an endpoint for sending messages
                // note that the CQ needs FI_RECV even though its a Tx cq to keep
                // some providers happy as they trigger an error if an endpoint
                // has no Rx cq attached (progress bug)
                auto ep_tx = new_endpoint_active(fabric_domain_, fabric_info_, true);
                bind_queue_to_endpoint(ep_tx, tx_cq, FI_TRANSMIT | FI_RECV, "tx threadlocal");
                bind_address_vector_to_endpoint(ep_tx, av_);
                enable_endpoint(ep_tx, "tx threadlocal");

                // set threadlocal endpoint wrapper
                LF_DEB(NS_DEBUG::cnb_deb,
                    trace(debug::str<>("Threadlocal Ep"), "create Tx", "ep", NS_DEBUG::ptr(ep_tx),
                        "tx cq", NS_DEBUG::ptr(tx_cq), "rx cq", NS_DEBUG::ptr(nullptr)));
                // for cleaning up at termination
                endpoint_wrapper ep(ep_tx, nullptr, tx_cq, "tx threadlocal");
                tx_endpoints_.push(ep);
                eps_->tl_tx_ = stack_endpoint(ep_tx, nullptr, tx_cq, "threadlocal", nullptr);
            }
            return eps_->tl_tx_.endpoint_;
        }
        else if (endpoint_type_ == endpoint_type::scalableTx ||
                 endpoint_type_ == endpoint_type::scalableTxRx)
        {
            if (eps_->tl_stx_.get_ep() == nullptr)
            {
                endpoint_wrapper ep;
                bool             ok = tx_endpoints_.pop(ep);
                if (!ok)
                {
                    LF_DEB(NS_DEBUG::cnb_deb,
                        error(debug::str<>("Scalable Ep"), "pop tx", "ep",
                            NS_DEBUG::ptr(ep.get_ep()), "tx cq", NS_DEBUG::ptr(ep.get_tx_cq()),
                            "rx cq", NS_DEBUG::ptr(ep.get_rx_cq())));
                    throw std::runtime_error("tx endpoint wrapper pop fail");
                }
                eps_->tl_stx_ = stack_endpoint(ep.get_ep(), ep.get_rx_cq(), ep.get_tx_cq(),
                    ep.get_name(), &tx_endpoints_);
                LF_DEB(NS_DEBUG::cnb_deb, trace(debug::str<>("Scalable Ep"), "pop tx", "ep",
                                              NS_DEBUG::ptr(eps_->tl_stx_.get_ep()), "tx cq",
                                              NS_DEBUG::ptr(eps_->tl_stx_.get_tx_cq()), "rx cq",
                                              NS_DEBUG::ptr(eps_->tl_stx_.get_rx_cq())));
            }
            return eps_->tl_stx_.endpoint_;
        }
        else if (endpoint_type_ == endpoint_type::multiple) { return eps_->ep_tx_; }
        // single : shared tx/rx endpoint
        return eps_->ep_rx_;
    }

    // --------------------------------------------------------------------
    void bind_address_vector_to_endpoint(struct fid_ep* endpoint, struct fid_av* av)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Binding AV"), "to", NS_DEBUG::ptr(endpoint)));
        int ret = fi_ep_bind(endpoint, &av->fid, 0);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "bind address_vector");
    }

    // --------------------------------------------------------------------
    void bind_queue_to_endpoint(struct fid_ep* endpoint, struct fid_cq*& cq, uint32_t cqtype,
        const char* type)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__, type);

        LF_DEB(NS_DEBUG::cnb_deb,
            debug(debug::str<>("Binding CQ"), "to", NS_DEBUG::ptr(endpoint), type));
        int ret = fi_ep_bind(endpoint, &cq->fid, cqtype);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "bind cq");
    }

    // --------------------------------------------------------------------
    fid_cq* bind_tx_queue_to_rx_endpoint(struct fi_info* info, struct fid_ep* ep)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);
        info->tx_attr->op_flags |= (FI_INJECT_COMPLETE | FI_COMPLETION);
        fid_cq* tx_cq = create_completion_queue(fabric_domain_, info->tx_attr->size, "tx->rx");
        // shared send/recv endpoint - bind send cq to the recv endpoint
        bind_queue_to_endpoint(ep, tx_cq, FI_TRANSMIT, "tx->rx bug fix");
        return tx_cq;
    }

    // --------------------------------------------------------------------
    void enable_endpoint(struct fid_ep* endpoint, const char* type)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__, type);

        LF_DEB(NS_DEBUG::cnb_deb,
            debug(debug::str<>("Enabling endpoint"), NS_DEBUG::ptr(endpoint)));
        int ret = fi_enable(endpoint);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "fi_enable");
    }

    // --------------------------------------------------------------------
    locality get_endpoint_address(struct fid* id)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        locality::locality_data local_addr;
        std::size_t             addrlen = locality_defs::array_size;
        int                     ret = fi_getname(id, local_addr.data(), &addrlen);
        if (ret || (addrlen > locality_defs::array_size))
        {
            std::string err =
                std::to_string(addrlen) + "=" + std::to_string(locality_defs::array_size);
            NS_LIBFABRIC::fabric_error(ret, "fi_getname - size error or other problem " + err);
        }

        // optimized out when debug logging is false
        if constexpr (NS_DEBUG::cnb_deb.is_enabled())
        {
            std::stringstream temp1;
            for (std::size_t i = 0; i < locality_defs::array_length; ++i)
            {
                temp1 << debug::ipaddr(&local_addr[i]) << " - ";
            }

            LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("raw address data"), "size",
                                          debug::dec<>(addrlen), " : ", temp1.str().c_str()));
            std::stringstream temp2;
            for (std::size_t i = 0; i < locality_defs::array_length; ++i)
            {
                temp2 << debug::hex<8>(local_addr[i]) << " - ";
            }
            LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("raw address data"), temp2.str().c_str()));
        }
        return locality(local_addr);
    }

    // --------------------------------------------------------------------
    fid_pep* create_passive_endpoint(struct fid_fabric* fabric, struct fi_info* info)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        struct fid_pep* ep;
        int             ret = fi_passive_ep(fabric, info, &ep, nullptr);
        if (ret) { throw NS_LIBFABRIC::fabric_error(ret, "Failed to create fi_passive_ep"); }
        return ep;
    }

    // --------------------------------------------------------------------
    inline const locality& here() const { return here_; }

    // --------------------------------------------------------------------
    inline const fi_addr_t& fi_address() const { return here_.fi_address(); }

    // --------------------------------------------------------------------
    inline void setHere(const locality& val) { here_ = val; }

    // --------------------------------------------------------------------
    inline const locality& root() const { return root_; }

    // --------------------------------------------------------------------
    inline struct fid_domain* get_domain() const { return fabric_domain_; }

    // --------------------------------------------------------------------
    inline std::size_t get_rma_protocol_size() { return 65536; }
#ifdef DISABLE_FI_INJECT
    // --------------------------------------------------------------------
    inline std::size_t get_tx_inject_size() { return 0; }
#else
    // --------------------------------------------------------------------
    inline std::size_t get_tx_inject_size() { return tx_inject_size_; }
#endif

    // --------------------------------------------------------------------
    inline std::size_t get_tx_size() { return tx_attr_size_; }

    // --------------------------------------------------------------------
    inline std::size_t get_rx_size() { return rx_attr_size_; }

    // --------------------------------------------------------------------
    // returns true when all connections have been disconnected and none are active
    inline bool isTerminated()
    {
        return false;
        //return (qp_endpoint_map_.size() == 0);
    }

    // --------------------------------------------------------------------
    void debug_print_av_vector(std::size_t N)
    {
        locality    addr;
        std::size_t addrlen = locality_defs::array_size;
        for (std::size_t i = 0; i < N; ++i)
        {
            int ret = fi_av_lookup(av_, fi_addr_t(i), addr.fabric_data_writable(), &addrlen);
            addr.set_fi_address(fi_addr_t(i));
            if ((ret == 0) && (addrlen == locality_defs::array_size))
            {
                LF_DEB(NS_DEBUG::cnb_deb,
                    debug(debug::str<>("address vector"), debug::dec<3>(i), iplocality(addr)));
            }
            else
            {
                LF_DEB(NS_DEBUG::cnb_err,
                    error(debug::str<>("address length"), debug::dec<3>(addrlen),
                        debug::dec<3>(locality_defs::array_size)));
                throw std::runtime_error("debug_print_av_vector : address vector "
                                         "traversal failure");
            }
        }
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
        return unique_lock(recv_mutex_);
    }

    // --------------------------------------------------------------------
    inline controller_base::unique_lock try_rx_lock()
    {
        if (bypass_rx_lock()) return unique_lock();
        return unique_lock(recv_mutex_, std::try_to_lock_t{});
    }

    // --------------------------------------------------------------------
    progress_status poll_for_work_completions(void* user_data)
    {
        progress_status p{0, 0};
        bool            retry = false;
        do {
            // sends
            uint32_t nsend = static_cast<Derived*>(this)->poll_send_queue(
                get_tx_endpoint().get_tx_cq(), user_data);
            p.m_num_sends += nsend;
            retry = (nsend == max_completions_per_poll_);
            // recvs
            uint32_t nrecv = static_cast<Derived*>(this)->poll_recv_queue(
                get_rx_endpoint().get_rx_cq(), user_data);
            p.m_num_recvs += nrecv;
            retry |= (nrecv == max_completions_per_poll_);
        } while (retry);
        return p;
    }

    // --------------------------------------------------------------------
    inline int poll_send_queue(fid_cq* tx_cq, void* user_data)
    {
        return static_cast<Derived*>(this)->poll_send_queue(tx_cq, user_data);
    }

    // --------------------------------------------------------------------
    inline int poll_recv_queue(fid_cq* rx_cq, void* user_data)
    {
        return static_cast<Derived*>(this)->poll_recv_queue(rx_cq, user_data);
    }

    // --------------------------------------------------------------------
    struct fid_cq* create_completion_queue(struct fid_domain* domain, size_t size, const char* type)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__, type);

        struct fid_cq* cq;
        fi_cq_attr     cq_attr = {};
        cq_attr.format = FI_CQ_FORMAT_MSG;
        cq_attr.wait_obj = FI_WAIT_NONE;
        cq_attr.wait_cond = FI_CQ_COND_NONE;
        cq_attr.size = size;
        cq_attr.flags = 0 /*FI_COMPLETION*/;
        LF_DEB(NS_DEBUG::cnb_deb, trace(debug::str<>("CQ size"), debug::dec<4>(size)));
        // open completion queue on fabric domain and set context to null
        int ret = fi_cq_open(domain, &cq_attr, &cq, nullptr);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "fi_cq_open");
        return cq;
    }

    // --------------------------------------------------------------------
    fid_av* create_address_vector(struct fi_info* info, int N, int num_rx_contexts)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        fid_av*    av;
        fi_av_attr av_attr = {fi_av_type(0), 0, 0, 0, nullptr, nullptr, 0};

        // number of addresses expected
        av_attr.count = N;

        // number of receive contexts used
        int rx_ctx_bits = 0;
#ifdef RX_CONTEXTS_SUPPORT
        while (num_rx_contexts >> ++rx_ctx_bits)
            ;
        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("rx_ctx_bits"), rx_ctx_bits));
#endif
        av_attr.rx_ctx_bits = rx_ctx_bits;
        // if contexts is nonzero, then we are using a single scalable endpoint
        av_attr.ep_per_node = (num_rx_contexts > 0) ? 2 : 0;

        if (info->domain_attr->av_type != FI_AV_UNSPEC)
        {
            av_attr.type = info->domain_attr->av_type;
        }
        else
        {
            LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("map FI_AV_TABLE")));
            av_attr.type = FI_AV_TABLE;
        }

        LF_DEB(NS_DEBUG::cnb_deb, debug(debug::str<>("Creating AV")));
        int ret = fi_av_open(fabric_domain_, &av_attr, &av, nullptr);
        if (ret) throw NS_LIBFABRIC::fabric_error(ret, "fi_av_open");
        return av;
    }

    // --------------------------------------------------------------------
    locality insert_address(const locality& address) { return insert_address(av_, address); }

    // --------------------------------------------------------------------
    locality insert_address(fid_av* av, const locality& address)
    {
        [[maybe_unused]] auto scp = NS_DEBUG::cnb_deb.scope(NS_DEBUG::ptr(this), __func__);

        LF_DEB(NS_DEBUG::cnb_deb,
            trace(debug::str<>("inserting AV"), iplocality(address), NS_DEBUG::ptr(av)));
        fi_addr_t fi_addr = 0xffffffff;
        int       ret = fi_av_insert(av, address.fabric_data(), 1, &fi_addr, 0, nullptr);
        if (ret < 0) { throw NS_LIBFABRIC::fabric_error(ret, "fi_av_insert"); }
        else if (ret == 0)
        {
            NS_DEBUG::cnb_deb.error("fi_av_insert called with existing address");
            NS_LIBFABRIC::fabric_error(ret, "fi_av_insert did not return 1");
        }
        // address was generated correctly, now update the locality with the fi_addr
        locality new_locality(address, fi_addr);
        LF_DEB(NS_DEBUG::cnb_deb, trace(debug::str<>("AV add"), "rank", debug::dec<>(fi_addr),
                                      iplocality(new_locality), "fi_addr", debug::hex<4>(fi_addr)));
        return new_locality;
    }
};

} // namespace NS_LIBFABRIC
