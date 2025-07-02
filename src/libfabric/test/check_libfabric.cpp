/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <iostream>
#include <iomanip>
#include <thread>
//
#include <mpi.h>
#include <oomph/context.hpp>
#include "../benchmarks/mpi_environment.hpp"
//
#include "../libfabric/context.hpp"
#include "../libfabric/communicator.hpp"
//
#include "../../context_base.hpp"
#include "../controller.hpp"

int main(int argc, char **argv)
{
    using namespace oomph;
    const bool message_pool_never_free = false;
    const std::size_t message_pool_reserve = 1024*1024*128;
    const bool multi_threaded = true;
    bool debug = true;
    //
    mpi_environment env(multi_threaded, argc, argv);
    auto ctxt = context_impl(MPI_COMM_WORLD, true, message_pool_never_free,  message_pool_reserve, debug);
}
