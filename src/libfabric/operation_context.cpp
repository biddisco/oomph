/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
// paths relative to backend
#include <oomph_libfabric_defines.hpp>
#include <controller.hpp>
#include <communicator.hpp>
#include <context.hpp>

namespace oomph::libfabric
{
void
operation_context::handle_cancelled()
{
    [[maybe_unused]] auto scp = opctx_deb<1>.scope(NS_DEBUG::ptr(this), __func__);
    // enqueue the cancelled/callback
    if (std::holds_alternative<detail::request_state*>(m_req))
    {
        // regular (non-shared) recv
        auto s = std::get<detail::request_state*>(m_req);
        while (!(s->m_comm->m_recv_cb_cancel.push(s))) {}
    }
    else if (std::holds_alternative<detail::shared_request_state*>(m_req))
    {
        // shared recv
        auto s = std::get<detail::shared_request_state*>(m_req);
        while (!(s->m_ctxt->m_recv_cb_cancel.push(s))) {}
    }
    else { throw std::runtime_error("Request state invalid in handle_cancelled"); }
}

int
operation_context::handle_tagged_recv_completion_impl(void* user_data)
{
    [[maybe_unused]] auto scp = opctx_deb<1>.scope(NS_DEBUG::ptr(this), __func__);
    if (std::holds_alternative<detail::request_state*>(m_req))
    {
        // regular (non-shared) recv
        auto s = std::get<detail::request_state*>(m_req);
        //if (std::this_thread::get_id() == thread_id_)
        if (reinterpret_cast<oomph::communicator_impl*>(user_data) == s->m_comm)
        {
            if (!s->m_comm->has_reached_recursion_depth())
            {
                auto inc = s->m_comm->recursion();
                auto ptr = s->release_self_ref();
                s->invoke_cb();
            }
            else
            {
                // enqueue the callback
                while (!(s->m_comm->m_recv_cb_queue.push(s))) {}
            }
        }
        else
        {
            // enqueue the callback
            while (!(s->m_comm->m_recv_cb_queue.push(s))) {}
        }
    }
    else if (std::holds_alternative<detail::shared_request_state*>(m_req))
    {
        // shared recv
        auto s = std::get<detail::shared_request_state*>(m_req);
        if (!s->m_comm->m_context->has_reached_recursion_depth())
        {
            auto inc = s->m_comm->m_context->recursion();
            auto ptr = s->release_self_ref();
            s->invoke_cb();
        }
        else
        {
            // enqueue the callback
            while (!(s->m_comm->m_context->m_recv_cb_queue.push(s))) {}
        }
    }
    else
    {
        detail::request_state** req = reinterpret_cast<detail::request_state**>(&m_req);
        LF_DEB(NS_MEMORY::opctx_deb<9>,
            error(NS_DEBUG::str<>("invalid request_state"), this, "request", NS_DEBUG::ptr(req)));
        throw std::runtime_error("Request state invalid in handle_tagged_recv");
    }
    return 1;
}

int
operation_context::handle_tagged_send_completion_impl(void* user_data)
{
    if (std::holds_alternative<detail::request_state*>(m_req))
    {
        // regular (non-shared) recv
        auto s = std::get<detail::request_state*>(m_req);
        if (reinterpret_cast<oomph::communicator_impl*>(user_data) == s->m_comm)
        {
            if (!s->m_comm->has_reached_recursion_depth())
            {
                auto inc = s->m_comm->recursion();
                auto ptr = s->release_self_ref();
                s->invoke_cb();
            }
            else
            {
                // enqueue the callback
                while (!(s->m_comm->m_send_cb_queue.push(s))) {}
            }
        }
        else
        {
            // enqueue the callback
            while (!(s->m_comm->m_send_cb_queue.push(s))) {}
        }
    }
    else if (std::holds_alternative<detail::shared_request_state*>(m_req))
    {
        // shared recv
        auto s = std::get<detail::shared_request_state*>(m_req);
        if (!s->m_comm->m_context->has_reached_recursion_depth())
        {
            auto inc = s->m_comm->m_context->recursion();
            auto ptr = s->release_self_ref();
            s->invoke_cb();
        }
        else
        {
            // enqueue the callback
            while (!(s->m_comm->m_context->m_recv_cb_queue.push(s))) {}
        }
    }
    else { throw std::runtime_error("Request state invalid in handle_tagged_send"); }
    return 1;
}
} // namespace oomph::libfabric
