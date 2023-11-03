/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <oomph/context.hpp>
#include <gtest/gtest.h>
#include "./mpi_runner/mpi_test_fixture.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>

#define NITERS   2
#define SIZE     64
#define NTHREADS 1

std::vector<std::atomic<int>> shared_received(NTHREADS);
thread_local int              thread_id;

void
reset_counters()
{
    for (auto& x : shared_received) x.store(0);
}

struct test_environment_base
{
    using rank_type = oomph::rank_type;
    using tag_type = oomph::tag_type;
    using message = oomph::message_buffer<rank_type>;

    oomph::context&     ctxt;
    oomph::communicator comm;
    rank_type           speer_rank;
    rank_type           rpeer_rank;
    int                 thread_id;
    int                 num_threads;
    tag_type            tag;

    test_environment_base(oomph::context& c, int tid, int num_t)
    : ctxt(c)
    , comm(ctxt.get_communicator())
    , speer_rank((comm.rank() + 1) % comm.size())
    , rpeer_rank((comm.rank() + comm.size() - 1) % comm.size())
    , thread_id(tid)
    , num_threads(num_t)
    , tag(tid)
    {
    }
};

struct test_environment : public test_environment_base
{
    using base = test_environment_base;

    static auto make_buffer(oomph::communicator& comm, std::size_t size, bool user_alloc,
        rank_type* ptr)
    {
        if (user_alloc) return comm.make_buffer<rank_type>(ptr, size);
        else
            return comm.make_buffer<rank_type>(size);
    }

    std::vector<rank_type> raw_smsg;
    std::vector<rank_type> raw_rmsg;
    message                smsg;
    message                rmsg;

    test_environment(oomph::context& c, std::size_t size, int tid, int num_t, bool user_alloc)
    : base(c, tid, num_t)
    , raw_smsg(user_alloc ? size : 0)
    , raw_rmsg(user_alloc ? size : 0)
    , smsg(make_buffer(comm, size, user_alloc, raw_smsg.data()))
    , rmsg(make_buffer(comm, size, user_alloc, raw_rmsg.data()))
    {
        fill_send_buffer();
        fill_recv_buffer();
    }

    void fill_send_buffer()
    {
        for (auto& x : smsg) x = comm.rank();
    }

    void fill_recv_buffer()
    {
        for (auto& x : rmsg) x = -1;
    }

    bool check_recv_buffer()
    {
        for (auto const& x : rmsg)
            if (x != rpeer_rank) return false;
        return true;
    }
};

#if HWMALLOC_ENABLE_DEVICE
struct test_environment_device : public test_environment_base
{
    using base = test_environment_base;

    static auto make_buffer(oomph::communicator& comm, std::size_t size, bool user_alloc,
        rank_type* device_ptr)
    {
        if (user_alloc) return comm.make_device_buffer<rank_type>(device_ptr, size, 0);
        else
            return comm.make_device_buffer<rank_type>(size, 0);
    }

    struct device_allocation
    {
        void* m_ptr = nullptr;
        device_allocation(std::size_t size = 0)
        {
            if (size) m_ptr = hwmalloc::device_malloc(size * sizeof(rank_type));
        }
        device_allocation(device_allocation&& other)
        : m_ptr{std::exchange(other.m_ptr, nullptr)}
        {
        }
        ~device_allocation()
        {
#ifndef OOMPH_TEST_LEAK_GPU_MEMORY
            if (m_ptr) hwmalloc::device_free(m_ptr);
#endif
        }
        rank_type* get() const noexcept { return (rank_type*)m_ptr; }
    };

    device_allocation raw_device_smsg;
    device_allocation raw_device_rmsg;
    message           smsg;
    message           rmsg;

    test_environment_device(oomph::context& c, std::size_t size, int tid, int num_t,
        bool user_alloc)
    : base(c, tid, num_t)
#ifndef OOMPH_TEST_LEAK_GPU_MEMORY
    , raw_device_smsg(user_alloc ? size : 0)
    , raw_device_rmsg(user_alloc ? size : 0)
    , smsg(make_buffer(comm, size, user_alloc, raw_device_smsg.get()))
    , rmsg(make_buffer(comm, size, user_alloc, raw_device_rmsg.get()))
#else
    , raw_device_smsg(size)
    , raw_device_rmsg(size)
    , smsg(make_buffer(comm, size, true, raw_device_smsg.get()))
    , rmsg(make_buffer(comm, size, true, raw_device_rmsg.get()))
#endif
    {
        fill_send_buffer();
        fill_recv_buffer();
    }

    void fill_send_buffer()
    {
        for (auto& x : smsg) x = comm.rank();
        smsg.clone_to_device();
    }

    void fill_recv_buffer()
    {
        for (auto& x : rmsg) x = -1;
        rmsg.clone_to_device();
    }

    bool check_recv_buffer()
    {
        rmsg.clone_to_host();
        for (auto const& x : rmsg)
            if (x != rpeer_rank) return false;
        return true;
    }
};
#endif

template<typename Func>
void
launch_test(Func f)
{
    // single threaded
    {
        oomph::context ctxt(MPI_COMM_WORLD, false);
        reset_counters();
        f(ctxt, SIZE, 0, 1, false);
        reset_counters();
        f(ctxt, SIZE, 0, 1, true);
    }

    // multi threaded
    {
        oomph::context           ctxt(MPI_COMM_WORLD, true);
        std::vector<std::thread> threads;
        threads.reserve(NTHREADS);
        reset_counters();
        for (int i = 0; i < NTHREADS; ++i)
            threads.push_back(std::thread{f, std::ref(ctxt), SIZE, i, NTHREADS, false});
        for (auto& t : threads) t.join();
        threads.clear();
        reset_counters();
        for (int i = 0; i < NTHREADS; ++i)
            threads.push_back(std::thread{f, std::ref(ctxt), SIZE, i, NTHREADS, true});
        for (auto& t : threads) t.join();
    }
}

// no callback
// ===========
template<typename Env>
void
test_send_recv(oomph::context& ctxt, std::size_t size, int tid, int num_threads, bool user_alloc)
{
    Env env(ctxt, size, tid, num_threads, user_alloc);

    // use is_ready() -> must manually progress the communicator
    for (int i = 0; i < NITERS; i++)
    {
        auto rreq = env.comm.recv(env.rmsg, env.rpeer_rank, env.tag);
        auto sreq = env.comm.send(env.smsg, env.speer_rank, env.tag);
        while (!(rreq.is_ready() && sreq.is_ready())) { env.comm.progress(); };
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }

    // use test() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rreq = env.comm.recv(env.rmsg, env.rpeer_rank, env.tag);
        auto sreq = env.comm.send(env.smsg, env.speer_rank, env.tag);
        while (!(rreq.test() && sreq.test())) {};
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }

    // use wait() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rreq = env.comm.recv(env.rmsg, env.rpeer_rank, env.tag);
        env.comm.send(env.smsg, env.speer_rank, env.tag).wait();
        rreq.wait();
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
}

TEST_F(mpi_test_fixture, send_recv)
{
    launch_test(test_send_recv<test_environment>);
    std::cout << "\n\n\n\n\n\n\n\n" << std::endl;
#if HWMALLOC_ENABLE_DEVICE
    launch_test(test_send_recv<test_environment_device>);
#endif
}
