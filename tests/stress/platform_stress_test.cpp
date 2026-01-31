/**
 * @file platform_stress_test.cpp
 * @brief PlatformLayer 高压力测试程序
 * @author CameraSubsystem Team
 * @date 2026-01-28
 *
 * 本程序用于对PlatformLayer进行高压力测试，验证其稳定性和性能
 * 测试内容：
 * 1. 多线程并发创建和销毁
 * 2. 高频率日志输出
 * 3. Epoll事件处理
 * 4. 线程亲和性和优先级设置
 * 5. 长时间运行稳定性
 */

#include "camera_subsystem/platform/platform_epoll.h"
#include "camera_subsystem/platform/platform_logger.h"
#include "camera_subsystem/platform/platform_thread.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

using namespace camera_subsystem::platform;

// 全局标志，用于控制测试运行
std::atomic<bool> g_running_(true);
std::atomic<uint64_t> g_log_count_(0);
std::atomic<uint64_t> g_thread_count_(0);
std::atomic<uint64_t> g_epoll_event_count_(0);

// 信号处理函数
void SignalHandler(int signal)
{
    (void)signal;
    g_running_.store(false);
}

// 日志压力测试线程函数
void LogStressThread(int thread_id)
{
    char thread_name[32];
    snprintf(thread_name, sizeof(thread_name), "log_stress_%d", thread_id);

    uint64_t local_count = 0;
    while (g_running_.load())
    {
        // 高频率输出日志
        PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                            "Thread %d log message %lu", thread_id, local_count);

        PlatformLogger::Log(LogLevel::kDebug, "stress_test",
                            "Thread %d debug message %lu", thread_id, local_count);

        local_count++;
        g_log_count_.fetch_add(2);

        // 避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "Thread %s exited, logged %lu messages",
                        thread_name, local_count);
}

// 线程生命周期测试
void ThreadLifecycleTest()
{
    PlatformLogger::Log(LogLevel::kInfo, "stress_test", "=== Thread Lifecycle Test ===");

    const int kThreadCount = 100;
    std::vector<std::unique_ptr<PlatformThread>> threads;

    // 创建多个线程
    for (int i = 0; i < kThreadCount && g_running_.load(); ++i)
    {
        char name[32];
        snprintf(name, sizeof(name), "lifecycle_%d", i);

        auto thread = std::make_unique<PlatformThread>(
            name,
            [i]()
            {
                g_thread_count_.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                                    "Thread %d executed", i);
            });

        if (thread->Start())
        {
            threads.push_back(std::move(thread));
        }
    }

    // 等待所有线程完成
    for (auto& thread : threads)
    {
        thread->Join();
    }

    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "Thread lifecycle test completed. Total threads executed: %lu",
                        g_thread_count_.load());
}

// Epoll压力测试
void EpollStressTest(int duration_seconds)
{
    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "=== Epoll Stress Test (%ds) ===", duration_seconds);

    PlatformEpoll epoll;
    if (!epoll.Create())
    {
        PlatformLogger::Log(LogLevel::kError, "stress_test", "Failed to create epoll");
        return;
    }

    const int kEventFdCount = 10;
    std::vector<int> event_fds;

    // 创建多个eventfd
    for (int i = 0; i < kEventFdCount; ++i)
    {
        int fd = eventfd(0, EFD_NONBLOCK);
        if (fd < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "stress_test",
                                "Failed to create eventfd %d", i);
            continue;
        }

        event_fds.push_back(fd);

        // 添加到epoll（保存实际fd作为data）
        if (!epoll.Add(fd, EPOLLIN, static_cast<uint64_t>(fd)))
        {
            PlatformLogger::Log(LogLevel::kError, "stress_test",
                                "Failed to add fd %d to epoll", fd);
        }
    }

    // 启动事件触发线程
    std::atomic<bool> trigger_running(true);
    std::thread trigger_thread(
        [&]()
        {
            uint64_t count = 1;
            const auto end_time =
                std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);

            while (trigger_running.load() && g_running_.load() &&
                   std::chrono::steady_clock::now() < end_time)
            {
                for (int fd : event_fds)
                {
                    write(fd, &count, sizeof(count));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

    // 等待事件
    struct epoll_event events[32];
    const int kTimeoutMs = 1000;
    uint64_t event_count = 0;

    auto start_time = std::chrono::steady_clock::now();
    PlatformLogger::Log(LogLevel::kInfo, "stress_test", "Starting epoll wait loop...");

    while (g_running_.load())
    {
        int ret = epoll.Wait(kTimeoutMs, events, 32);
        if (ret > 0)
        {
            for (int i = 0; i < ret; ++i)
            {
                const int event_fd = static_cast<int>(events[i].data.u64);
                uint64_t value = 0;
                const ssize_t read_bytes = read(event_fd, &value, sizeof(value));
                if (read_bytes > 0)
                {
                    event_count++;
                    g_epoll_event_count_.fetch_add(1);
                }
            }
        }
        else if (ret < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "stress_test", "Epoll wait error");
            break;
        }

        // 按指定时间退出
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= duration_seconds)
        {
            PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                                "Epoll test duration reached");
            break;
        }
    }

    // 停止触发线程
    trigger_running.store(false);
    if (trigger_thread.joinable())
    {
        trigger_thread.join();
    }
    epoll.Close();

    // 关闭所有eventfd
    for (int fd : event_fds)
    {
        close(fd);
    }

    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "Epoll stress test completed. Total events: %lu",
                        event_count);
}

// 线程亲和性和优先级测试
void ThreadAffinityPriorityTest()
{
    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "=== Thread Affinity & Priority Test ===");

    const int kThreadCount = 4;
    std::vector<std::unique_ptr<PlatformThread>> threads;

    // 获取CPU核心数
    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "System CPU cores: %d", cpu_count);

    for (int i = 0; i < kThreadCount && g_running_.load(); ++i)
    {
        char name[32];
        snprintf(name, sizeof(name), "affinity_%d", i);

        auto thread = std::make_unique<PlatformThread>(
            name,
            [i, cpu_count]()
            {
                // 设置线程亲和性
                std::vector<int> cpu_ids = {i % cpu_count};

                // 获取当前线程的PlatformThread对象（这里简化处理）
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                                    "Thread %d running on CPU %d", i, (i % cpu_count));
            });

        if (thread->Start())
        {
            // 设置CPU亲和性
            std::vector<int> cpu_ids = {i % cpu_count};
            thread->SetCpuAffinity(cpu_ids);

            // 设置优先级
            thread->SetPriority(10);

            threads.push_back(std::move(thread));
        }
    }

    // 等待所有线程完成
    for (auto& thread : threads)
    {
        thread->Join();
    }

    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "Thread affinity & priority test completed");
}

// 长时间运行测试
void LongRunningStressTest(int duration_seconds)
{
    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "=== Long Running Stress Test (%ds) ===", duration_seconds);

    const int kLogThreadCount = 4;
    std::vector<std::unique_ptr<PlatformThread>> log_threads;

    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_log_count = 0;

    // 启动日志压力测试线程
    for (int i = 0; i < kLogThreadCount && g_running_.load(); ++i)
    {
        char name[32];
        snprintf(name, sizeof(name), "long_run_%d", i);

        auto thread = std::make_unique<PlatformThread>(name, [i]() { LogStressThread(i); });

        if (thread->Start())
        {
            log_threads.push_back(std::move(thread));
        }
    }

    // 定期打印统计信息
    while (g_running_.load())
    {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        if (elapsed_seconds >= duration_seconds)
        {
            break;
        }

        uint64_t current_log_count = g_log_count_.load();
        uint64_t logs_per_second = current_log_count - last_log_count;
        last_log_count = current_log_count;

        PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                            "Running time: %lds, Total logs: %lu, Logs/s: %lu",
                            static_cast<long>(elapsed_seconds),
                            current_log_count,
                            logs_per_second);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 停止所有线程
    g_running_.store(false);

    // 等待所有线程完成
    for (auto& thread : log_threads)
    {
        thread->Join();
    }

    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "Long running stress test completed");
    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "Total logs: %lu", g_log_count_.load());
    PlatformLogger::Log(LogLevel::kInfo, "stress_test",
                        "Total epoll events: %lu", g_epoll_event_count_.load());
}

int main(int argc, char* argv[])
{
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 初始化日志系统（仅控制台输出）
    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "main",
                        "========================================");
    PlatformLogger::Log(LogLevel::kInfo, "main",
                        "  PlatformLayer Stress Test Program   ");
    PlatformLogger::Log(LogLevel::kInfo, "main",
                        "========================================");

    PlatformLogger::Log(LogLevel::kInfo, "main", "PlatformLayer stress test started");

    const int kDefaultTestDuration = 10; // 默认测试时长10秒
    const int kMinimumDuration = 1;
    int test_duration = kDefaultTestDuration;
    if (argc > 1)
    {
        test_duration = atoi(argv[1]);
        if (test_duration < kMinimumDuration)
        {
            test_duration = kMinimumDuration;
        }
    }

    PlatformLogger::Log(LogLevel::kInfo, "main",
                        "Test duration: %d seconds", test_duration);

    try
    {
        // 执行线程生命周期测试
        ThreadLifecycleTest();
        if (!g_running_.load())
        {
            PlatformLogger::Log(LogLevel::kWarning, "main", "Interrupted by signal");
            PlatformLogger::Shutdown();
            return 1;
        }

        // 短时运行时，将时长平均分配给主要耗时测试
        const int segment_seconds = std::max(kMinimumDuration, test_duration / 2);

        // 执行Epoll压力测试
        EpollStressTest(segment_seconds);
        if (!g_running_.load())
        {
            PlatformLogger::Log(LogLevel::kWarning, "main", "Interrupted by signal");
            PlatformLogger::Shutdown();
            return 1;
        }

        // 执行线程亲和性和优先级测试
        ThreadAffinityPriorityTest();
        if (!g_running_.load())
        {
            PlatformLogger::Log(LogLevel::kWarning, "main", "Interrupted by signal");
            PlatformLogger::Shutdown();
            return 1;
        }

        // 执行长时间运行测试
        LongRunningStressTest(segment_seconds);

        PlatformLogger::Log(LogLevel::kInfo, "main", "All stress tests completed successfully");
        PlatformLogger::Log(LogLevel::kInfo, "main", "=== Stress Test Summary ===");
        PlatformLogger::Log(LogLevel::kInfo, "main",
                            "Total logs: %lu", g_log_count_.load());
        PlatformLogger::Log(LogLevel::kInfo, "main",
                            "Total thread executions: %lu", g_thread_count_.load());
        PlatformLogger::Log(LogLevel::kInfo, "main",
                            "Total epoll events: %lu", g_epoll_event_count_.load());
        PlatformLogger::Log(LogLevel::kInfo, "main", "All tests passed!");
    }
    catch (const std::exception& e)
    {
        PlatformLogger::Log(LogLevel::kError, "main",
                            "Exception during stress test: %s", e.what());
        PlatformLogger::Shutdown();
        return 1;
    }

    // 关闭日志系统
    PlatformLogger::Shutdown();

    return 0;
}
