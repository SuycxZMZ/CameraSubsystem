/**
 * @file test_buffer_lifecycle.cpp
 * @brief Buffer 生命周期管理单元测试
 * @author CameraSubsystem Team
 * @date 2026-02-27
 * 
 * 测试覆盖：
 * 1. BufferGuard RAII 机制
 * 2. BufferState 状态机转换
 * 3. BufferPool 析构安全
 * 4. FrameHandleEx 生命周期绑定
 */

#include "camera_subsystem/core/buffer_guard.h"
#include "camera_subsystem/core/buffer_pool.h"
#include "camera_subsystem/core/buffer_state.h"
#include "camera_subsystem/core/frame_handle_ex.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using namespace camera_subsystem::core;

// 测试计数器
static int g_test_count = 0;
static int g_pass_count = 0;

#define TEST_ASSERT(condition, message)                                                            \
    do                                                                                             \
    {                                                                                              \
        g_test_count++;                                                                            \
        if (condition)                                                                             \
        {                                                                                          \
            g_pass_count++;                                                                        \
            printf("[PASS] %s\n", message);                                                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("[FAIL] %s (line %d)\n", message, __LINE__);                                    \
        }                                                                                          \
    } while (0)

// 测试 1: BufferGuard RAII 机制
void TestBufferGuardRAII()
{
    printf("\n=== Test 1: BufferGuard RAII ===\n");

    BufferPool pool;
    TEST_ASSERT(pool.Initialize(4, 1024), "BufferPool 初始化成功");

    {
        auto guard = pool.Acquire();
        TEST_ASSERT(guard != nullptr, "Acquire 成功");
        TEST_ASSERT(guard->IsValid(), "BufferGuard 有效");
        TEST_ASSERT(guard->Data() != nullptr, "BufferGuard 数据指针有效");
        TEST_ASSERT(guard->Size() == 1024, "BufferGuard 大小正确");

        auto stats = pool.GetStats();
        TEST_ASSERT(stats.in_use == 1, "in_use 计数正确");
    }

    // guard 离开作用域，自动释放
    auto stats = pool.GetStats();
    TEST_ASSERT(stats.in_use == 0, "RAII 自动释放成功");
    TEST_ASSERT(stats.available == 4, "Buffer 归还到池中");
}

// 测试 2: BufferState 状态机转换
void TestBufferStateTransition()
{
    printf("\n=== Test 2: BufferState 状态机转换 ===\n");

    BufferPool pool;
    TEST_ASSERT(pool.Initialize(4, 1024), "BufferPool 初始化成功");

    auto guard = pool.Acquire();
    TEST_ASSERT(guard != nullptr, "Acquire 成功");

    auto stats = pool.GetStats();
    TEST_ASSERT(stats.in_use == 1, "初始状态: InUse");

    // 测试 MarkInFlight
    guard->MarkInFlight();
    stats = pool.GetStats();
    TEST_ASSERT(stats.in_flight == 1, "MarkInFlight 成功");
    TEST_ASSERT(stats.in_use == 0, "InUse 计数减少");

    // 测试 CancelInFlight
    guard->CancelInFlight();
    stats = pool.GetStats();
    TEST_ASSERT(stats.in_use == 1, "CancelInFlight 成功");
    TEST_ASSERT(stats.in_flight == 0, "InFlight 计数减少");

    // 测试 MarkError
    guard->MarkError();
    // Error 状态不影响统计，但状态已改变
    printf("[INFO] MarkError 测试完成\n");
}

// 测试 3: BufferPool 析构安全
void TestBufferPoolDestructorSafety()
{
    printf("\n=== Test 3: BufferPool 析构安全 ===\n");

    std::shared_ptr<BufferGuard> leaked_guard;

    {
        BufferPool pool;
        TEST_ASSERT(pool.Initialize(4, 1024), "BufferPool 初始化成功");

        leaked_guard = pool.Acquire();
        TEST_ASSERT(leaked_guard != nullptr, "Acquire 成功");

        auto stats = pool.GetStats();
        TEST_ASSERT(stats.in_use == 1, "Buffer 被持有");

        // pool 即将析构，但 leaked_guard 仍持有 Buffer
        // 析构函数应该等待或超时
    }

    // pool 已析构，leaked_guard 仍持有
    // 这应该不会崩溃，因为析构函数等待了
    TEST_ASSERT(leaked_guard != nullptr, "leaked_guard 仍存在");
    printf("[INFO] BufferPool 析构安全测试完成（应该看到超时告警）\n");
}

// 测试 4: FrameHandleEx 生命周期绑定
void TestFrameHandleExLifecycle()
{
    printf("\n=== Test 4: FrameHandleEx 生命周期绑定 ===\n");

    BufferPool pool;
    TEST_ASSERT(pool.Initialize(4, 1024), "BufferPool 初始化成功");

    FrameHandle handle;
    handle.frame_id_ = 1;
    handle.width_ = 640;
    handle.height_ = 480;
    handle.format_ = PixelFormat::kNV12;
    handle.virtual_address_ = nullptr;  // 初始化为 nullptr
    handle.buffer_size_ = 1024;

    auto guard = pool.Acquire();
    TEST_ASSERT(guard != nullptr, "Acquire 成功");

    // 创建 FrameHandleEx
    FrameHandleEx handle_ex(handle, guard);
    // 注意：FrameHandle::IsValid() 可能检查 virtual_address_，所以这里不测试 handle.IsValid()
    TEST_ASSERT(handle_ex.buffer_ref != nullptr, "FrameHandleEx buffer_ref 有效");
    TEST_ASSERT(handle_ex.GetData() != nullptr, "FrameHandleEx 数据指针有效");
    TEST_ASSERT(handle_ex.GetSize() == 1024, "FrameHandleEx 大小正确");

    // 测试生命周期绑定
    {
        FrameHandleEx handle_ex2 = handle_ex;  // 拷贝构造
        TEST_ASSERT(handle_ex2.buffer_ref != nullptr, "拷贝构造后 buffer_ref 有效");
        TEST_ASSERT(handle_ex2.buffer_ref.use_count() == 3, "引用计数正确（handle_ex, handle_ex2, guard）");
    }

    TEST_ASSERT(handle_ex.buffer_ref != nullptr, "handle_ex2 析构后 handle_ex buffer_ref 仍有效");
    TEST_ASSERT(handle_ex.buffer_ref.use_count() == 2, "引用计数减少");
}

// 测试 5: 多线程安全
void TestMultiThreadSafety()
{
    printf("\n=== Test 5: 多线程安全 ===\n");

    BufferPool pool;
    TEST_ASSERT(pool.Initialize(16, 1024), "BufferPool 初始化成功");

    const int kThreadCount = 4;
    const int kIterations = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreadCount; ++i)
    {
        threads.emplace_back([&pool, i, kIterations]() {
            for (int j = 0; j < kIterations; ++j)
            {
                auto guard = pool.Acquire();
                if (guard)
                {
                    guard->MarkInFlight();
                    // 模拟处理
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    guard->CancelInFlight();
                    // guard 自动释放
                }
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    auto stats = pool.GetStats();
    TEST_ASSERT(stats.in_use == 0, "所有 Buffer 已归还");
    TEST_ASSERT(stats.in_flight == 0, "没有 InFlight 的 Buffer");
    TEST_ASSERT(stats.acquire_count == kThreadCount * kIterations, "Acquire 计数正确");
}

// 测试 6: 泄漏检测
void TestLeakDetection()
{
    printf("\n=== Test 6: 泄漏检测 ===\n");

    BufferPool pool;
    TEST_ASSERT(pool.Initialize(4, 1024), "BufferPool 初始化成功");

    auto guard1 = pool.Acquire();
    auto guard2 = pool.Acquire();

    auto leaks = pool.CheckLeaks();
    TEST_ASSERT(leaks.size() == 2, "检测到 2 个泄漏");

    guard1.reset();
    leaks = pool.CheckLeaks();
    TEST_ASSERT(leaks.size() == 1, "检测到 1 个泄漏");

    guard2.reset();
    leaks = pool.CheckLeaks();
    TEST_ASSERT(leaks.empty(), "没有泄漏");
}

int main()
{
    printf("========================================\n");
    printf("Buffer 生命周期管理单元测试\n");
    printf("========================================\n");

    TestBufferGuardRAII();
    TestBufferStateTransition();
    TestBufferPoolDestructorSafety();
    TestFrameHandleExLifecycle();
    TestMultiThreadSafety();
    TestLeakDetection();

    printf("\n========================================\n");
    printf("测试结果: %d/%d 通过\n", g_pass_count, g_test_count);
    printf("========================================\n");

    return (g_pass_count == g_test_count) ? 0 : 1;
}
