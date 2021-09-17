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
#include <mpi.h>
#include <string.h>
#include <atomic>

// do not include OOMPH functionality
#define OOMPH_BENCHMARKS_PURE_MPI
#include "./mpi_environment.hpp"
#include "./args.hpp"
#include "./timer.hpp"
#include "./utils.hpp"

#ifdef OOMPH_BENCHMARKS_MT
#include <omp.h>
#endif /* OOMPH_BENCHMARKS_MT */

#ifdef USE_WAITALL
const char* syncmode = "waitall";
#else
const char* syncmode = "wait";
#endif
const char* waitmode = "wait";

int
main(int argc, char* argv[])
{
    using namespace oomph;

    int   rank, size, peer_rank;
    timer t0, t1;

    args cmd_args(argc, argv);
    if (!cmd_args) return exit(argv[0]);
    bool const multi_threaded = (cmd_args.num_threads > 1);

    mpi_environment env(multi_threaded, argc, argv);
    if (env.size != 2) return exit(argv[0]);

    const auto inflight = cmd_args.inflight;
    const auto buff_size = cmd_args.buff_size;
    const auto niter = cmd_args.n_iter;

    if (env.rank == 0)
    {
        std::cout << "inflight = " << inflight << std::endl;
        std::cout << "size     = " << buff_size << std::endl;
        std::cout << "N        = " << niter << std::endl;
    }

#ifdef OOMPH_BENCHMARKS_MT
#pragma omp parallel
#endif
    {
        int             thrid = 0, nthr = 1;
        MPI_Comm        mpi_comm = MPI_COMM_NULL;
        unsigned char** sbuffers = new unsigned char*[inflight];
        unsigned char** rbuffers = new unsigned char*[inflight];
        MPI_Request*    sreq = new MPI_Request[inflight];
        MPI_Request*    rreq = new MPI_Request[inflight];

#ifdef OOMPH_BENCHMARKS_MT
#pragma omp master
#endif
        {
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            peer_rank = (rank + 1) % 2;
            if (rank == 0) std::cout << "\n\nrunning test " << __FILE__ << "\n\n";
        }

#ifdef OOMPH_BENCHMARKS_MT
        thrid = omp_get_thread_num();
        nthr = omp_get_num_threads();
#endif

        /* duplicate the communicator - all threads in order */
        for (int tid = 0; tid < nthr; tid++)
        {
            if (thrid == tid) { MPI_Comm_dup(MPI_COMM_WORLD, &mpi_comm); }
#ifdef OOMPH_BENCHMARKS_MT
#pragma omp barrier
#endif
        }

        for (int j = 0; j < inflight; j++)
        {
            MPI_Alloc_mem(buff_size, MPI_INFO_NULL, &sbuffers[j]);
            MPI_Alloc_mem(buff_size, MPI_INFO_NULL, &rbuffers[j]);
            memset(sbuffers[j], 1, buff_size);
            memset(rbuffers[j], 1, buff_size);
            sreq[j] = MPI_REQUEST_NULL;
            rreq[j] = MPI_REQUEST_NULL;
        }

        if (thrid == 0) MPI_Barrier(mpi_comm);
#ifdef OOMPH_BENCHMARKS_MT
#pragma omp barrier
#endif

        if (thrid == 0)
        {
            t0.tic();
            t1.tic();
            if (rank == 1)
                std::cout << "number of threads: " << nthr << ", multi-threaded: " << multi_threaded
                          << "\n";
        }

        int  i = 0, dbg = 0;
        int  last_i = 0;
        char header[256];
        snprintf(header, 256, "%d total bwdt ", rank);
        while (i < niter)
        {
            if (thrid == 0 && dbg >= (niter / 10))
            {
                dbg = 0;
                t0.vtoc(header, (double)(i - last_i) * size * buff_size);
                t0.tic();
                last_i = i;
            }

            /* submit comm */
            for (int j = 0; j < inflight; j++)
            {
                MPI_Irecv(rbuffers[j], buff_size, MPI_BYTE, peer_rank, thrid * inflight + j,
                    mpi_comm, &rreq[j]);
                MPI_Isend(sbuffers[j], buff_size, MPI_BYTE, peer_rank, thrid * inflight + j,
                    mpi_comm, &sreq[j]);
                dbg += nthr;
                i += nthr;
            }

            /* wait for all to complete */
#ifdef USE_WAITALL
            MPI_Waitall(inflight, sreq, MPI_STATUS_IGNORE);
            MPI_Waitall(inflight, rreq, MPI_STATUS_IGNORE);
#else
            for (int j = 0; j < inflight; j++)
            {
                MPI_Wait(rreq + j, MPI_STATUS_IGNORE);
                MPI_Wait(sreq + j, MPI_STATUS_IGNORE);
            }
#endif
#ifdef OOMPH_BENCHMARKS_MT
#pragma omp barrier
#endif
        }
        delete []sbuffers;
        delete []rbuffers;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 1)
    {
        double elapsed = t1.toc();
        t1.vtoc();
        t1.vtoc("final ", (double)niter * size * buff_size);
        double bw = ((double)(niter * size) * buff_size) / elapsed;
        // clang-format off
        std::cout << "time:       " << elapsed/1000000 << "s\n";
        std::cout << "final MB/s: " << bw << "\n";
        std::cout << "CSVData"
                  << ", niter, " << niter
                  << ", buff_size, " << buff_size
                  << ", inflight, " << inflight
                  << ", num_threads, " << cmd_args.num_threads
                  << ", syncmode, " << syncmode
                  << ", waitmode, " << waitmode
                  << ", transport, " << "Native-MPI"
                  << ", BW MB/s, " << bw
                  << ", progress, " << "unspecified"
                  << ", endpoint, " << "unspecified"
                  << "\n";
        // clang-format on
    }

    return 0;
}
