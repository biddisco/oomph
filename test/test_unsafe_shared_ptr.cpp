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
#include <oomph/util/unsafe_shared_ptr.hpp>
#include "./ctor_stats.hpp"
#include "./reporting_allocator.hpp"

struct my_int
{
    ctor_stats m_stats;
    int        m_i;

    my_int(ctor_stats_data& d, int i)
    : m_stats{d}
    , m_i(i)
    {
    }

    int get() const noexcept { return m_i; }
};

using alloc_t = reporting_allocator<char>;

TEST(unsafe_shared_ptr, ctor)
{
    using namespace oomph::util;
    {
        ctor_stats_data d;
        {
            unsafe_shared_ptr<my_int> a(alloc_t(), d, 42);
            unsafe_shared_ptr<my_int> b;

            EXPECT_TRUE(a);
            EXPECT_EQ(a.use_count(), 1u);
            EXPECT_FALSE(b);
            EXPECT_EQ(b.use_count(), 0u);

            EXPECT_EQ((*a).get(), 42);
            EXPECT_EQ(a.get()->get(), 42);
            EXPECT_EQ(a->get(), 42);
        }
        EXPECT_EQ(d.alloc_ref_count, 0);
    }
    {
        ctor_stats_data d;
        {
            auto a = oomph::util::allocate_shared<my_int>(alloc_t(), d, 42);

            EXPECT_TRUE(a);
            EXPECT_EQ(a.use_count(), 1u);

            EXPECT_EQ((*a).get(), 42);
            EXPECT_EQ(a.get()->get(), 42);
            EXPECT_EQ(a->get(), 42);
        }
        EXPECT_EQ(d.alloc_ref_count, 0);
    }
    {
        ctor_stats_data d;
        {
            auto a = oomph::util::make_shared<my_int>(d, 42);

            EXPECT_TRUE(a);
            EXPECT_EQ(a.use_count(), 1u);

            EXPECT_EQ((*a).get(), 42);
            EXPECT_EQ(a.get()->get(), 42);
            EXPECT_EQ(a->get(), 42);
        }
        EXPECT_EQ(d.alloc_ref_count, 0);
    }
}

TEST(unsafe_shared_ptr, assign)
{
    using namespace oomph::util;
    {
        ctor_stats_data d;
        {
            unsafe_shared_ptr<my_int> a(alloc_t(), d, 42);
            unsafe_shared_ptr<my_int> b(a);

            EXPECT_TRUE(a);
            EXPECT_EQ(a.use_count(), 2u);
            EXPECT_EQ(a->get(), 42);
            EXPECT_TRUE(b);
            EXPECT_EQ(b.use_count(), 2u);
            EXPECT_EQ(b->get(), 42);
        }
        EXPECT_EQ(d.alloc_ref_count, 0);
    }
    {
        ctor_stats_data d;
        {
            unsafe_shared_ptr<my_int> a(alloc_t(), d, 42);
            unsafe_shared_ptr<my_int> b;

            EXPECT_FALSE(b);
            EXPECT_EQ(b.use_count(), 0u);

            b = a;

            EXPECT_TRUE(a);
            EXPECT_EQ(a.use_count(), 2u);
            EXPECT_EQ(a->get(), 42);
            EXPECT_TRUE(b);
            EXPECT_EQ(b.use_count(), 2u);
            EXPECT_EQ(b->get(), 42);
        }
        EXPECT_EQ(d.alloc_ref_count, 0);
    }
}

TEST(unsafe_shared_ptr, move_assign)
{
    using namespace oomph::util;
    {
        ctor_stats_data d;
        {
            unsafe_shared_ptr<my_int> a(alloc_t(), d, 42);
            unsafe_shared_ptr<my_int> b(std::move(a));

            EXPECT_FALSE(a);
            EXPECT_EQ(a.use_count(), 0u);
            EXPECT_TRUE(b);
            EXPECT_EQ(b.use_count(), 1u);
            EXPECT_EQ(b->get(), 42);
        }
        EXPECT_EQ(d.alloc_ref_count, 0);
    }
    {
        ctor_stats_data d;
        {
            unsafe_shared_ptr<my_int> a(alloc_t(), d, 42);
            unsafe_shared_ptr<my_int> b;

            EXPECT_FALSE(b);
            EXPECT_EQ(b.use_count(), 0u);

            b = std::move(a);

            EXPECT_FALSE(a);
            EXPECT_EQ(a.use_count(), 0u);
            EXPECT_TRUE(b);
            EXPECT_EQ(b.use_count(), 1u);
            EXPECT_EQ(b->get(), 42);
        }
        EXPECT_EQ(d.alloc_ref_count, 0);
    }
}

int
main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
