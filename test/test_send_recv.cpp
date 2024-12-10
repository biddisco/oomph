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

#define NITERS   50
#define SIZE     64
#define NTHREADS 4

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
#if HWMALLOC_ENABLE_DEVICE
    launch_test(test_send_recv<test_environment_device>);
#endif
}

// callback: pass by l-value reference
// ===================================
template<typename Env>
void
test_send_recv_cb(oomph::context& ctxt, std::size_t size, int tid, int num_threads, bool user_alloc)
{
    using rank_type = test_environment::rank_type;
    using tag_type = test_environment::tag_type;
    using message = test_environment::message;

    Env env(ctxt, size, tid, num_threads, user_alloc);

    volatile int received = 0;
    volatile int sent = 0;

    auto send_callback = [&](message const&, rank_type, tag_type) { ++sent; };
    auto recv_callback = [&](message&, rank_type, tag_type) { ++received; };

    // use is_ready() -> must manually progress the communicator
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.recv(env.rmsg, env.rpeer_rank, 1, recv_callback);
        auto sh = env.comm.send(env.smsg, env.speer_rank, 1, send_callback);
        while (!rh.is_ready() || !sh.is_ready()) { env.comm.progress(); }
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(received, NITERS);
    EXPECT_EQ(sent, NITERS);

    received = 0;
    sent = 0;
    // use test() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.recv(env.rmsg, env.rpeer_rank, 1, recv_callback);
        auto sh = env.comm.send(env.smsg, env.speer_rank, 1, send_callback);
        while (!rh.test() || !sh.test()) {}
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(received, NITERS);
    EXPECT_EQ(sent, NITERS);

    received = 0;
    sent = 0;
    // use wait() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.recv(env.rmsg, env.rpeer_rank, 1, recv_callback);
        env.comm.send(env.smsg, env.speer_rank, 1, send_callback).wait();
        rh.wait();
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(received, NITERS);
    EXPECT_EQ(sent, NITERS);
}

TEST_F(mpi_test_fixture, send_recv_cb)
{
    launch_test(test_send_recv_cb<test_environment>);
#if HWMALLOC_ENABLE_DEVICE
    launch_test(test_send_recv_cb<test_environment_device>);
#endif
}

// callback: pass by r-value reference (give up ownership)
// =======================================================
template<typename Env>
void
test_send_recv_cb_disown(oomph::context& ctxt, std::size_t size, int tid, int num_threads,
    bool user_alloc)
{
    using rank_type = test_environment::rank_type;
    using tag_type = test_environment::tag_type;
    using message = test_environment::message;

    Env env(ctxt, size, tid, num_threads, user_alloc);

    volatile int received = 0;
    volatile int sent = 0;

    auto send_callback = [&](message msg, rank_type, tag_type)
    {
        ++sent;
        env.smsg = std::move(msg);
    };
    auto recv_callback = [&](message msg, rank_type, tag_type)
    {
        ++received;
        env.rmsg = std::move(msg);
    };

    // use is_ready() -> must manually progress the communicator
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.recv(std::move(env.rmsg), env.rpeer_rank, 1, recv_callback);
        auto sh = env.comm.send(std::move(env.smsg), env.speer_rank, 1, send_callback);
        while (!rh.is_ready() || !sh.is_ready()) { env.comm.progress(); }
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(received, NITERS);
    EXPECT_EQ(sent, NITERS);

    received = 0;
    sent = 0;
    // use test() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.recv(std::move(env.rmsg), env.rpeer_rank, 1, recv_callback);
        auto sh = env.comm.send(std::move(env.smsg), env.speer_rank, 1, send_callback);
        while (!rh.test() || !sh.test()) {}
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(received, NITERS);
    EXPECT_EQ(sent, NITERS);

    received = 0;
    sent = 0;
    // use wait() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.recv(std::move(env.rmsg), env.rpeer_rank, 1, recv_callback);
        env.comm.send(std::move(env.smsg), env.speer_rank, 1, send_callback).wait();
        rh.wait();
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(received, NITERS);
    EXPECT_EQ(sent, NITERS);
}

TEST_F(mpi_test_fixture, send_recv_cb_disown)
{
    launch_test(test_send_recv_cb_disown<test_environment>);
#if HWMALLOC_ENABLE_DEVICE
    launch_test(test_send_recv_cb_disown<test_environment_device>);
#endif
}

// callback: pass by r-value reference (give up ownership), shared recv
// ====================================================================
template<typename Env>
void
test_send_shared_recv_cb_disown(oomph::context& ctxt, std::size_t size, int tid, int num_threads,
    bool user_alloc)
{
    using rank_type = test_environment::rank_type;
    using tag_type = test_environment::tag_type;
    using message = test_environment::message;

    Env env(ctxt, size, tid, num_threads, user_alloc);

    thread_id = env.thread_id;

    //volatile int received = 0;
    volatile int sent = 0;

    auto send_callback = [&](message msg, rank_type, tag_type)
    {
        ++sent;
        env.smsg = std::move(msg);
    };
    auto recv_callback = [&](message msg, rank_type, tag_type)
    {
        //std::cout << thread_id << " " << env.thread_id << std::endl;
        //if (thread_id != env.thread_id) std::cout << "other thread picked up callback" << std::endl;
        //else std::cout << "my thread picked up callback" << std::endl;
        env.rmsg = std::move(msg);
        ++shared_received[env.thread_id];
    };

    // use is_ready() -> must manually progress the communicator
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.shared_recv(std::move(env.rmsg), env.rpeer_rank, 1, recv_callback);
        auto sh = env.comm.send(std::move(env.smsg), env.speer_rank, 1, send_callback);
        while (!rh.is_ready() || !sh.is_ready()) { env.comm.progress(); }
        EXPECT_TRUE(env.rmsg);
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(shared_received[env.thread_id].load(), NITERS);
    EXPECT_EQ(sent, NITERS);

    shared_received[env.thread_id].store(0);
    sent = 0;
    // use test() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.shared_recv(std::move(env.rmsg), env.rpeer_rank, 1, recv_callback);
        auto sh = env.comm.send(std::move(env.smsg), env.speer_rank, 1, send_callback);
        while (!rh.test() || !sh.test()) {}
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(shared_received[env.thread_id].load(), NITERS);
    EXPECT_EQ(sent, NITERS);

    shared_received[env.thread_id].store(0);
    sent = 0;
    // use wait() -> communicator is progressed automatically
    for (int i = 0; i < NITERS; i++)
    {
        auto rh = env.comm.shared_recv(std::move(env.rmsg), env.rpeer_rank, 1, recv_callback);
        env.comm.send(std::move(env.smsg), env.speer_rank, 1, send_callback).wait();
        rh.wait();
        EXPECT_TRUE(env.check_recv_buffer());
        env.fill_recv_buffer();
    }
    EXPECT_EQ(shared_received[env.thread_id].load(), NITERS);
    EXPECT_EQ(sent, NITERS);
}

TEST_F(mpi_test_fixture, send_shared_recv_cb_disown)
{
    launch_test(test_send_shared_recv_cb_disown<test_environment>);
#if HWMALLOC_ENABLE_DEVICE
    launch_test(test_send_shared_recv_cb_disown<test_environment_device>);
#endif
}

// callback: pass by l-value reference, and resubmit
// =================================================
template<typename Env>
void
test_send_recv_cb_resubmit(oomph::context& ctxt, std::size_t size, int tid, int num_threads,
    bool user_alloc)
{
    using rank_type = test_environment::rank_type;
    using tag_type = test_environment::tag_type;
    using message = test_environment::message;

    Env env(ctxt, size, tid, num_threads, user_alloc);

    volatile int received = 0;
    volatile int sent = 0;

    struct recursive_send_callback
    {
        Env&          env;
        volatile int& sent;

        void operator()(message& msg, rank_type dst, tag_type tag)
        {
            ++sent;
            if (sent < NITERS) env.comm.send(msg, dst, tag, recursive_send_callback{*this});
        }
    };

    struct recursive_recv_callback
    {
        Env&          env;
        volatile int& received;

        void operator()(message& msg, rank_type src, tag_type tag)
        {
            ++received;
            EXPECT_TRUE(env.check_recv_buffer());
            env.fill_recv_buffer();
            if (received < NITERS) env.comm.recv(msg, src, tag, recursive_recv_callback{*this});
        }
    };

    env.comm.recv(env.rmsg, env.rpeer_rank, 1, recursive_recv_callback{env, received});
    env.comm.send(env.smsg, env.speer_rank, 1, recursive_send_callback{env, sent});

    while (sent < NITERS || received < NITERS) { env.comm.progress(); };
}

TEST_F(mpi_test_fixture, send_recv_cb_resubmit)
{
    launch_test(test_send_recv_cb_resubmit<test_environment>);
#if HWMALLOC_ENABLE_DEVICE
    launch_test(test_send_recv_cb_resubmit<test_environment_device>);
#endif
}

// callback: pass by r-value reference (give up ownership), and resubmit
// =====================================================================
template<typename Env>
void
test_send_recv_cb_resubmit_disown(oomph::context& ctxt, std::size_t size, int tid, int num_threads,
    bool user_alloc)
{
    using rank_type = test_environment::rank_type;
    using tag_type = test_environment::tag_type;
    using message = test_environment::message;

    Env env(ctxt, size, tid, num_threads, user_alloc);

    volatile int received = 0;
    volatile int sent = 0;

    struct recursive_send_callback
    {
        Env&          env;
        volatile int& sent;

        void operator()(message msg, rank_type dst, tag_type tag)
        {
            ++sent;
            if (sent < NITERS)
                env.comm.send(std::move(msg), dst, tag, recursive_send_callback{*this});
        }
    };

    struct recursive_recv_callback
    {
        Env&          env;
        volatile int& received;

        void operator()(message msg, rank_type src, tag_type tag)
        {
            ++received;
            env.rmsg = std::move(msg);
            EXPECT_TRUE(env.check_recv_buffer());
            env.fill_recv_buffer();
            if (received < NITERS)
                env.comm.recv(std::move(env.rmsg), src, tag, recursive_recv_callback{*this});
        }
    };

    env.comm.recv(std::move(env.rmsg), env.rpeer_rank, 1, recursive_recv_callback{env, received});
    env.comm.send(std::move(env.smsg), env.speer_rank, 1, recursive_send_callback{env, sent});

    while (sent < NITERS || received < NITERS) { env.comm.progress(); };
}

TEST_F(mpi_test_fixture, send_recv_cb_resubmit_disown)
{
    launch_test(test_send_recv_cb_resubmit_disown<test_environment>);
#if HWMALLOC_ENABLE_DEVICE
    launch_test(test_send_recv_cb_resubmit_disown<test_environment_device>);
#endif
}
