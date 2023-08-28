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

#include <hwmalloc/register.hpp>
#include <hwmalloc/heap.hpp>
#include <oomph/config.hpp>

// paths relative to backend
#include <region.hpp>
#include <lock_cache.hpp>

namespace oomph
{
class rma_context
{
  public:
    using region_type = rma_region;
    using device_region_type = rma_region;
    using heap_type = hwmalloc::heap<rma_context>;

  private:
    struct mpi_win_holder
    {
        MPI_Win m;
        ~mpi_win_holder() { MPI_Win_free(&m); }
    };

  private:
    MPI_Comm                    m_mpi_comm;
    mpi_win_holder              m_win;
    heap_type                   m_heap;
    std::unique_ptr<lock_cache> m_lock_cache;

  public:
    rma_context(MPI_Comm comm)
    : m_mpi_comm{comm}
    , m_heap{this}
    {
        MPI_Info info;
        OOMPH_CHECK_MPI_RESULT(MPI_Info_create(&info));
        OOMPH_CHECK_MPI_RESULT(MPI_Info_set(info, "no_locks", "false"));
        OOMPH_CHECK_MPI_RESULT(MPI_Win_create_dynamic(info, m_mpi_comm, &(m_win.m)));
        MPI_Info_free(&info);
        OOMPH_CHECK_MPI_RESULT(MPI_Win_fence(0, m_win.m));
        m_lock_cache = std::make_unique<lock_cache>(m_win.m);
    }
    rma_context(context_impl const&) = delete;
    rma_context(context_impl&&) = delete;

    rma_region make_region(void* ptr, std::size_t size) const
    {
        return {m_mpi_comm, m_win.m, ptr, size};
    }

    auto  get_window() const noexcept { return m_win.m; }
    auto& get_heap() noexcept { return m_heap; }
    void  lock(rank_type r) { m_lock_cache->lock(r); }
};

template<>
inline rma_region
register_memory<rma_context>(rma_context& c, void* ptr, std::size_t size)
{
    return c.make_region(ptr, size);
}

#if OOMPH_ENABLE_DEVICE
template<>
inline rma_region
register_device_memory<rma_context>(rma_context& c, int, void* ptr, std::size_t size)
{
    return c.make_region(ptr, size);
}
#endif

} // namespace oomph
