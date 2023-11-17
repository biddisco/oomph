/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <gtest/gtest.h>
#include <oomph/util/unique_function.hpp>
#include "ctor_stats.hpp"

// test a simple function
// which has an overloaded call operator
// =====================================

struct simple_function
{
    int i = 0;

    simple_function(int i_ = 0)
    : i{i_}
    {
    }

    simple_function(simple_function const&) = default;
    simple_function& operator=(simple_function const&) = default;

    void operator()() { i = 0; }

    int operator()(int k)
    {
        auto const tmp = i;
        i = k;
        return tmp;
    }
};

TEST(unqiue_function, simple_function)
{
    simple_function                        f1(1);
    simple_function                        f2(2);
    simple_function                        f3(0);
    oomph::util::unique_function<int(int)> uf1{std::move(f1)};
    EXPECT_EQ(1, uf1(3));
    EXPECT_EQ(3, uf1(4));
    oomph::util::unique_function<int(int)> uf2{std::move(f2)};
    EXPECT_EQ(2, uf2(3));
    EXPECT_EQ(3, uf2(5));
    uf1 = std::move(f3);
    EXPECT_EQ(0, uf1(3));
    EXPECT_EQ(3, uf1(4));
}

void test_stats(ctor_stats_data const& stats, int n_ctor, int n_dtor, int n_dtor_of_moved,
    int n_move_ctor, int n_calls);

// small function which fits within the stack buffer
struct small_function
{
    ctor_stats stats;

    small_function(ctor_stats_data& d)
    : stats{d}
    {
    }

    small_function(small_function const&) = delete;
    small_function& operator=(small_function const&) = delete;

    small_function(small_function&&) = default;
    small_function& operator=(small_function&&) = default;

    void operator()()
    {
        if (!stats) throw std::runtime_error("invoked from invalid state!");
        stats.call();
    }
};

// small function which requires allocation
struct large_function : public small_function
{
    std::array<char, 256> buffer;

    large_function(ctor_stats_data& d)
    : small_function(d)
    {
    }
};

// test (move) constructor
// =======================

template<typename F1, typename F2>
void
test_ctor(function_registry& registry)
{
    using namespace oomph::util;

    {
        unique_function<void()> uf(registry.template make<F1>("a_F1_0"));
        uf();
        uf();
    }
    {
        auto                    f = registry.template make<F1>("b_F1_0");
        unique_function<void()> uf(std::move(f));
        uf();
        uf();
    }
    {
        auto                    f1 = registry.template make<F1>("c_F1_0");
        auto                    f2 = registry.template make<F1>("c_F1_1");
        unique_function<void()> uf;
        uf = std::move(f1);
        uf();
        uf = std::move(f2);
        uf();
    }
    {
        auto                    f1 = registry.template make<F1>("d_F1_0");
        auto                    f2 = registry.template make<F2>("d_F2_0");
        unique_function<void()> uf;
        uf = std::move(f1);
        uf();
        uf = std::move(f2);
        uf();
    }
}

TEST(unqiue_function, ctor_small)
{
    function_registry registry;

    test_ctor<small_function, large_function>(registry);

    test_stats(registry["a_F1_0"], 1, 1, 1, 1, 2);

    test_stats(registry["b_F1_0"], 1, 1, 1, 1, 2);

    test_stats(registry["c_F1_0"], 1, 1, 1, 1, 1);
    test_stats(registry["c_F1_1"], 1, 1, 1, 1, 1);

    test_stats(registry["d_F1_0"], 1, 1, 1, 1, 1);
    test_stats(registry["d_F2_0"], 1, 1, 1, 1, 1);
}

TEST(unqiue_function, ctor_large)
{
    function_registry registry;

    test_ctor<large_function, small_function>(registry);

    test_stats(registry["a_F1_0"], 1, 1, 1, 1, 2);

    test_stats(registry["b_F1_0"], 1, 1, 1, 1, 2);

    test_stats(registry["c_F1_0"], 1, 1, 1, 1, 1);
    test_stats(registry["c_F1_1"], 1, 1, 1, 1, 1);

    test_stats(registry["d_F1_0"], 1, 1, 1, 1, 1);
    test_stats(registry["d_F2_0"], 1, 1, 1, 1, 1);
}

// test move assign
// ================

template<typename F1, typename F2>
void
test_move(function_registry& registry)
{
    using namespace oomph::util;

    {
        unique_function<void()> uf(registry.template make<F1>("a_F1_0"));
        uf();
        uf();
        uf = registry.template make<F1>("a_F1_1");
        uf();
        uf();
    }
    {
        unique_function<void()> uf1(registry.template make<F1>("b_F1_0"));
        uf1();
        uf1();
        unique_function<void()> uf2(registry.template make<F1>("b_F1_1"));
        uf2();
        uf1 = std::move(uf2);
        uf1();
    }
    {
        unique_function<void()> uf1(registry.template make<F1>("c_F1_0"));
        uf1();
        unique_function<void()> uf2(std::move(uf1));
        uf2();
        unique_function<void()> uf3(std::move(uf2));
        uf3();
    }
    {
        unique_function<void()> uf1(registry.template make<F1>("d_F1_0"));
        uf1();
        uf1();
        unique_function<void()> uf2(registry.template make<F2>("d_F2_0"));
        uf2();
        uf1 = std::move(uf2);
        uf1();
    }
}

TEST(unqiue_function, move_small)
{
    function_registry registry;

    test_move<small_function, large_function>(registry);

    test_stats(registry["a_F1_0"], 1, 1, 1, 1, 2);
    test_stats(registry["a_F1_1"], 1, 1, 1, 1, 2);

    test_stats(registry["b_F1_0"], 1, 1, 1, 1, 2);
    test_stats(registry["b_F1_1"], 1, 1, 2, 2, 2);

    test_stats(registry["c_F1_0"], 1, 1, 3, 3, 3);

    test_stats(registry["d_F1_0"], 1, 1, 1, 1, 2);
    test_stats(registry["d_F2_0"], 1, 1, 1, 1, 2);
}

TEST(unqiue_function, move_large)
{
    function_registry registry;

    test_move<large_function, small_function>(registry);

    test_stats(registry["a_F1_0"], 1, 1, 1, 1, 2);
    test_stats(registry["a_F1_1"], 1, 1, 1, 1, 2);

    test_stats(registry["b_F1_0"], 1, 1, 1, 1, 2);
    test_stats(registry["b_F1_1"], 1, 1, 1, 1, 2);

    test_stats(registry["c_F1_0"], 1, 1, 1, 1, 3);

    test_stats(registry["d_F1_0"], 1, 1, 1, 1, 2);
    test_stats(registry["d_F2_0"], 1, 1, 2, 2, 2);
}

// implementation of check function
void
test_stats(ctor_stats_data const& stats, int n_ctor, int n_dtor, int n_dtor_of_moved,
    int n_move_ctor, int n_calls)
{
    EXPECT_EQ(stats.n_ctor, n_ctor);
    EXPECT_EQ(stats.n_dtor, n_dtor);
    EXPECT_EQ(stats.n_dtor_of_moved, n_dtor_of_moved);
    EXPECT_EQ(stats.n_move_ctor, n_move_ctor);
    EXPECT_EQ(stats.n_calls, n_calls);

    EXPECT_EQ(stats.n_move_ctor_of_moved, 0);
    EXPECT_EQ(stats.n_move_assign, 0);
    EXPECT_EQ(stats.n_move_assign_to_moved, 0);
    EXPECT_EQ(stats.n_move_assign_of_moved, 0);
    EXPECT_EQ(stats.n_move_assign_of_moved_to_moved, 0);
    EXPECT_EQ(stats.alloc_ref_count, 0);

    //std::cout << stats << std::endl;
}

int
main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
