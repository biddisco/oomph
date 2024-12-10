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

#include <mpi.h>
#include <iostream>

#ifdef OOMPH_BENCHMARKS_mpi
#define TRANSPORT_STRING "mpi"
#define PROGRESS_STRING  "unspecified"
#define ENDPOINT_STRING  "unspecified"
#endif

#ifdef OOMPH_BENCHMARKS_ucx
#define TRANSPORT_STRING "ucx"
#define PROGRESS_STRING  "unspecified"
#define ENDPOINT_STRING  "unspecified"
#endif

#ifdef OOMPH_BENCHMARKS_libfabric
#include "../src/libfabric/controller.hpp"
#define TRANSPORT_STRING "libfabric"
#define PROGRESS_STRING  LIBFABRIC_PROGRESS_STRING
#define ENDPOINT_STRING  LIBFABRIC_ENDPOINT_STRING
#endif

namespace oomph
{
struct mpi_environment
{
    int size;
    int rank;

    mpi_environment(bool thread_safe, int& argc, char**& argv)
    {
        int mode;
        if (thread_safe)
        {
            MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &mode);
            if (mode != MPI_THREAD_MULTIPLE)
            {
                std::cerr << "MPI_THREAD_MULTIPLE not supported by MPI, aborting\n";
                std::terminate();
            }
        }
        else { MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &mode); }
        MPI_Comm_size(MPI_COMM_WORLD, &size);
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    }

    mpi_environment(mpi_environment const&) = delete;

    ~mpi_environment()
    {
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
    }
};

} // namespace oomph
