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

#include <variant>
//
#include <oomph/request.hpp>
//
#include "operation_context_base.hpp"
//
namespace oomph::libfabric
{

template <int Level>
inline /*constexpr*/ NS_DEBUG::print_threshold<Level, 0> opctx_deb("OP__CXT");

// This struct holds the ready state of a future
// we must also store the context used in libfabric, in case
// a request is cancelled - fi_cancel(...) needs it
struct operation_context : public operation_context_base<operation_context>
{
    std::variant<oomph::detail::request_state*, oomph::detail::shared_request_state*> m_req;

    template<typename RequestState>
    operation_context(RequestState* req)
    : operation_context_base(/*ctx_any*/)
    , m_req{req}
    {
        [[maybe_unused]] auto scp = opctx_deb<9>.scope(NS_DEBUG::ptr(this), __func__, "request", req);
    }

    // --------------------------------------------------------------------
    // When a completion returns FI_ECANCELED, this is called
    void handle_cancelled();

    // --------------------------------------------------------------------
    // Called when a tagged recv completes
    int handle_tagged_recv_completion_impl(void* user_data);

    // --------------------------------------------------------------------
    // Called when a tagged send completes
    int handle_tagged_send_completion_impl(void* user_data);
};

} // namespace oomph::libfabric
