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

#include <rdma/fi_eq.h>
#include "oomph_libfabric_defines.hpp"

namespace NS_LIBFABRIC
{

class controller;

enum operation_context_type : int32_t
{
    ctx_unknown = 0,
    ctx_sender = 1,
    ctx_receiver = 2,
    ctx_rma = 3,
    ctx_any = 4,
};

// This struct holds the ready state of a future
// we must also store the context used in libfabric, in case
// a request is cancelled - fi_cancel(...) needs it
template<typename Derived>
struct operation_context_base
{
  private:
    // libfabric requires some space for it's internal bookkeeping
    // so the first member of this struct must be fi_context
    fi_context             context_reserved_space;
    operation_context_type type_;

  public:
    operation_context_base(operation_context_type ctype)
    : context_reserved_space()
    , type_{ctype}
    {
    }

    // type is needed to smiplify the dispatch of errors
    operation_context_type get_context_type() { return type_; }

    // error
    void handle_error(struct fi_cq_err_entry& err)
    {
        static_cast<Derived*>(this)->handle_error_impl(err);
    }
    void handle_error_impl(struct fi_cq_err_entry& /*err*/) { std::terminate(); }

    void handle_cancelled() { static_cast<Derived*>(this)->handle_cancelled_impl(); }
    void handle_cancelled_impl() { std::terminate(); }

    // send
    int handle_send_completion()
    {
        return static_cast<Derived*>(this)->handle_send_completion_impl();
    }
    int handle_send_completion_impl() { return 0; }

    // tagged send
    int handle_tagged_send_completion(void* user_data)
    {
        return static_cast<Derived*>(this)->handle_tagged_send_completion_impl(user_data);
    }
    int handle_tagged_send_completion_impl(void* /*user_data*/) { return 0; }

    // recv
    int handle_recv_completion(std::uint64_t len)
    {
        return static_cast<Derived*>(this)->handle_recv_completion_impl(len);
    }
    int handle_recv_completion_impl(std::uint64_t /*len*/) { return 0; }

    // tagged recv
    int handle_tagged_recv_completion(void* user_data)
    {
        return static_cast<Derived*>(this)->handle_tagged_recv_completion_impl(user_data);
    }
    int handle_tagged_recv_completion_impl(bool /*threadlocal*/) { return 0; }

    void handle_rma_read_completion()
    {
        static_cast<Derived*>(this)->handle_rma_read_completion_impl();
    }
    void handle_rma_read_completion_impl() {}

    // unknown sender = new connection
    int handle_new_connection(controller* ctrl, std::uint64_t len)
    {
        return static_cast<Derived*>(this)->handle_new_connection_impl(ctrl, len);
    }
    int handle_new_connection_impl(controller*, std::uint64_t) { return 0; }
};

// provided so that a pointer can be cast to this and the operation_context_type queried
struct unspecialized_context : public operation_context_base<unspecialized_context>
{
};
} // namespace NS_LIBFABRIC
