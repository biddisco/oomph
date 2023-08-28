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
#include <hwmalloc/register_device.hpp>
#include <hwmalloc/heap.hpp>

#include <oomph/config.hpp>

// paths relative to backend
#include <config.hpp>
#include <region.hpp>

namespace oomph
{
class rma_context
{
  public:
    using region_type = rma_region;
    using device_region_type = rma_region;
    using heap_type = hwmalloc::heap<rma_context>;

  private:
    heap_type     m_heap;
    ucp_context_h m_context;

  public:
    rma_context()
    : m_heap{this}
    {
    }
    rma_context(context_impl const&) = delete;
    rma_context(context_impl&&) = delete;

    rma_region make_region(void* ptr, std::size_t size, bool gpu = false)
    {
        return {m_context, ptr, size, gpu};
    }

    auto& get_heap() noexcept { return m_heap; }

    void set_ucp_context(ucp_context_h c) { m_context = c; }
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
    return c.make_region(ptr, size, true);
}
#endif

} // namespace oomph
