/**
 * @file test_buffer_pool.cpp
 * @brief BufferPool 单元测试
 * @author CameraSubsystem Team
 * @date 2026-02-02
 */

#include <gtest/gtest.h>
#include "camera_subsystem/core/buffer_guard.h"

using namespace camera_subsystem::core;

TEST(BufferPoolTest, InitializeAndAcquire)
{
    BufferPool pool;
    EXPECT_TRUE(pool.Initialize(4, 1024));

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.total, 4u);
    EXPECT_EQ(stats.available, 4u);

    auto b1 = pool.Acquire();
    auto b2 = pool.Acquire();
    auto b3 = pool.Acquire();
    auto b4 = pool.Acquire();
    auto b5 = pool.Acquire();

    EXPECT_NE(b1, nullptr);
    EXPECT_NE(b2, nullptr);
    EXPECT_NE(b3, nullptr);
    EXPECT_NE(b4, nullptr);
    EXPECT_EQ(b5, nullptr);

    stats = pool.GetStats();
    EXPECT_EQ(stats.available, 0u);
    EXPECT_EQ(stats.in_use, 4u);
    EXPECT_EQ(stats.in_flight, 0u);

    b1.reset();
    stats = pool.GetStats();
    EXPECT_EQ(stats.available, 1u);
    EXPECT_EQ(stats.in_use, 3u);

    b2.reset();
    b3.reset();
    b4.reset();
}

TEST(BufferPoolTest, ReuseBuffer)
{
    BufferPool pool;
    EXPECT_TRUE(pool.Initialize(2, 256));

    auto b1 = pool.Acquire();
    auto b2 = pool.Acquire();
    EXPECT_NE(b1, nullptr);
    EXPECT_NE(b2, nullptr);
    EXPECT_EQ(pool.Acquire(), nullptr);

    const uint32_t id_before = b1->Id();
    b1.reset();

    auto b3 = pool.Acquire();
    ASSERT_NE(b3, nullptr);
    EXPECT_EQ(b3->Id(), id_before);

    b2.reset();
    b3.reset();
}

TEST(BufferPoolTest, StatsCounters)
{
    BufferPool pool;
    EXPECT_TRUE(pool.Initialize(1, 128));

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.acquire_count, 0u);
    EXPECT_EQ(stats.release_count, 0u);
    EXPECT_EQ(stats.acquire_fail, 0u);

    auto b1 = pool.Acquire();
    auto b2 = pool.Acquire();
    b1.reset();
    b2.reset();

    stats = pool.GetStats();
    EXPECT_EQ(stats.acquire_count, 2u);
    EXPECT_EQ(stats.acquire_fail, 1u);
    EXPECT_EQ(stats.release_count, 1u);
}

TEST(BufferPoolTest, InFlightStateAndLeakCheck)
{
    BufferPool pool;
    EXPECT_TRUE(pool.Initialize(2, 256));

    auto b1 = pool.Acquire();
    ASSERT_NE(b1, nullptr);

    auto leaks = pool.CheckLeaks();
    ASSERT_EQ(leaks.size(), 1u);
    EXPECT_EQ(leaks[0], b1->Id());

    b1->MarkInFlight();
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.in_use, 0u);
    EXPECT_EQ(stats.in_flight, 1u);

    b1.reset();
    stats = pool.GetStats();
    EXPECT_EQ(stats.in_flight, 0u);
    EXPECT_EQ(stats.available, 2u);
}
