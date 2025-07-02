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

#include <stdexcept>
#include <string>
#include <string.h>
//
#include <rdma/fi_errno.h>
//
#include "oomph_libfabric_defines.hpp"

namespace NS_DEBUG
{
// cppcheck-suppress ConfigurationNotChecked
static NS_DEBUG::enable_print<true> err_deb("ERROR__");
} // namespace NS_DEBUG

namespace NS_LIBFABRIC
{

class fabric_error : public std::runtime_error
{
  public:
    // --------------------------------------------------------------------
    fabric_error(int err, const std::string& msg)
    : std::runtime_error(std::string(fi_strerror(-err)) + msg)
    , error_(err)
    {
        NS_DEBUG::err_deb.error(msg, ":", fi_strerror(-err));
        std::terminate();
    }

    fabric_error(int err)
    : std::runtime_error(fi_strerror(-err))
    , error_(-err)
    {
        NS_DEBUG::err_deb.error(what());
        std::terminate();
    }

    int error_;
};

} // namespace NS_LIBFABRIC
