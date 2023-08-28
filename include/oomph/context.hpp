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

#include <hwmalloc/config.hpp>
#include <hwmalloc/device.hpp>
#include <oomph/config.hpp>
#include <oomph/message_buffer.hpp>
#include <oomph/communicator.hpp>
#include <oomph/util/mpi_comm_holder.hpp>
#include <oomph/util/heap_pimpl.hpp>

namespace oomph
{
class context_impl;
class barrier;
class context
{
    friend class barrier;

  public:
    using pimpl = util::heap_pimpl<context_impl>;

  public:
    struct schedule
    {
        std::atomic<std::size_t> scheduled_sends = 0;
        std::atomic<std::size_t> scheduled_recvs = 0;
    };

  private:
    util::mpi_comm_holder     m_mpi_comm;
    pimpl                     m;
    std::unique_ptr<schedule> m_schedule;

  public:
    context(MPI_Comm comm, bool thread_safe = true,
        bool message_pool_never_free = false, std::size_t message_pool_reserve = 1);

    context(context const&) = delete;

    context(context&&) noexcept = default;

    context& operator=(context const&) = delete;

    context& operator=(context&&) noexcept = default;

    ~context();

  public:
    rank_type rank() const noexcept;

    rank_type size() const noexcept;

    rank_type local_rank() const noexcept;

    rank_type local_size() const noexcept;

    MPI_Comm mpi_comm() const noexcept { return m_mpi_comm.get(); }

    template<typename T>
    message_buffer<T> make_buffer(std::size_t size)
    {
        return {make_buffer_core(size * sizeof(T)), size};
    }

    template<typename T>
    message_buffer<T> make_buffer(T* ptr, std::size_t size)
    {
        return {make_buffer_core(ptr, size * sizeof(T)), size};
    }

#if OOMPH_ENABLE_DEVICE
    template<typename T>
    message_buffer<T> make_device_buffer(std::size_t size, int id = hwmalloc::get_device_id())
    {
        return {make_buffer_core(size * sizeof(T), id), size};
    }

    template<typename T>
    message_buffer<T> make_device_buffer(T* device_ptr, std::size_t size,
        int id = hwmalloc::get_device_id())
    {
        return {make_buffer_core(device_ptr, size * sizeof(T), id), size};
    }

    template<typename T>
    message_buffer<T> make_device_buffer(T* ptr, T* device_ptr, std::size_t size,
        int id = hwmalloc::get_device_id())
    {
        return {make_buffer_core(ptr, device_ptr, size * sizeof(T), id), size};
    }
#endif

    communicator get_communicator();//unsigned int tag_range = 0);

    //unsigned int num_tag_ranges() const noexcept { return m_tag_range_factory.num_ranges(); }

    const char* get_transport_option(const std::string& opt);

  private:
    detail::message_buffer make_buffer_core(std::size_t size);
    detail::message_buffer make_buffer_core(void* ptr, std::size_t size);
#if OOMPH_ENABLE_DEVICE
    detail::message_buffer make_buffer_core(std::size_t size, int device_id);
    detail::message_buffer make_buffer_core(void* device_ptr, std::size_t size, int device_id);
    detail::message_buffer make_buffer_core(void* ptr, void* device_ptr, std::size_t size,
        int device_id);
#endif
};

template<typename Context>
typename Context::region_type register_memory(Context&, void*, std::size_t);
#if OOMPH_ENABLE_DEVICE
template<typename Context>
typename Context::device_region_type register_device_memory(Context&, int, void*, std::size_t);
#endif

} // namespace oomph
