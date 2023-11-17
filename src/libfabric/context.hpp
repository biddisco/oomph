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

#include <thread>
#include <stack>

#include <hwmalloc/heap.hpp>
#include <hwmalloc/register.hpp>

#include <oomph/config.hpp>

// paths relative to backend
#include <../context_base.hpp>
#include <memory_region.hpp>
#include <controller.hpp>
#include <request_state.hpp>

namespace oomph
{

static NS_DEBUG::enable_print<true> ctx_deb("CONTEXT");

using controller_type = libfabric::controller;

class context_impl : public context_base
{
  public:
    using region_type = libfabric::memory_segment;
    using domain_type = region_type::provider_domain;
    using device_region_type = libfabric::memory_segment;
    using heap_type = hwmalloc::heap<context_impl>;
    using callback_queue = boost::lockfree::queue<detail::shared_request_state*,
        boost::lockfree::fixed_sized<false>, boost::lockfree::allocator<std::allocator<void>>>;

  private:
    heap_type                        m_heap;
    domain_type*                     m_domain;
    std::shared_ptr<controller_type> m_controller;
    std::uintptr_t                   m_ctxt_tag;

  public:
    // --------------------------------------------------
    // create a singleton ptr to a libfabric controller that
    // can be shared between oomph context objects
    static std::shared_ptr<controller_type> init_libfabric_controller(oomph::context_impl* ctx,
        MPI_Comm comm, int rank, int size, int threads);

    // queue for shared recv callbacks
    callback_queue m_recv_cb_queue;
    // queue for canceled shared recv requests
    callback_queue m_recv_cb_cancel;

  public:
    context_impl(MPI_Comm comm, bool thread_safe, bool message_pool_never_free,
        std::size_t message_pool_reserve);
    context_impl(context_impl const&) = delete;
    context_impl(context_impl&&) = delete;

    region_type make_region(void* const ptr, std::size_t size, int device_id)
    {
        bool bind_mr = ((m_controller->memory_registration_mode_flags() & FI_MR_ENDPOINT) != 0);
        if (bind_mr)
        {
            void* endpoint = m_controller->get_rx_endpoint().get_ep();
            return libfabric::memory_segment(m_domain, ptr, size, bind_mr, endpoint, device_id);
        }
        else { return libfabric::memory_segment(m_domain, ptr, size, false, nullptr, device_id); }
    }

    auto& get_heap() noexcept { return m_heap; }

    communicator_impl* get_communicator();

    // we must modify all tags to use 32bits of context ptr for uniqueness
    inline std::uintptr_t get_context_tag() { return m_ctxt_tag; }

    inline controller_type* get_controller() /*const */ { return m_controller.get(); }
    const char*             get_transport_option(const std::string& opt);

    void progress() { get_controller()->poll_for_work_completions(nullptr); }

    bool cancel_recv(detail::shared_request_state* s)
    {
        // get the original message operation context
        auto op_ctx = &(s->m_operation_context);

        // submit the cancellation request
        bool ok = (fi_cancel(&(get_controller()->get_rx_endpoint().get_ep()->fid), op_ctx) == 0);

        // if the cancel operation failed completely, return
        if (!ok) return false;

        bool found = false;
        while (!found)
        {
            get_controller()->poll_recv_queue(get_controller()->get_rx_endpoint().get_rx_cq(),
                nullptr);
            // otherwise, poll until we know if it worked
            std::stack<detail::shared_request_state*> temp_stack;
            detail::shared_request_state*             temp;
            while (!found && m_recv_cb_cancel.pop(temp))
            {
                if (temp == s)
                {
                    // our recv was cancelled correctly
                    found = true;
                    LF_DEB(oomph::ctx_deb, debug(NS_DEBUG::str<>("Cancel shared"), "succeeded",
                                               "op_ctx", NS_DEBUG::ptr(op_ctx)));
                    auto ptr = s->release_self_ref();
                    s->set_canceled();
                }
                else
                {
                    // a different cancel operation
                    temp_stack.push(temp);
                }
            }
            // return any weird unhandled cancels back to the queue
            while (!temp_stack.empty())
            {
                auto temp = temp_stack.top();
                temp_stack.pop();
                m_recv_cb_cancel.push(temp);
            }
        }
        return found;
    }

    unsigned int num_tag_bits() const noexcept { return 32; }
};

// --------------------------------------------------------------------
template<>
inline oomph::libfabric::memory_segment
register_memory<oomph::context_impl>(oomph::context_impl& c, void* const ptr, std::size_t size)
{
    return c.make_region(ptr, size, -2);
}

#if OOMPH_ENABLE_DEVICE
template<>
inline oomph::libfabric::memory_segment
register_device_memory<context_impl>(context_impl& c, int device_id, void* ptr, std::size_t size)
{
    return c.make_region(ptr, size, device_id);
}
#endif

} // namespace oomph
