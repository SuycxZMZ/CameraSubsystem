/**
 * @file frame_broker_stress_test.cpp
 * @brief FrameBroker 压力测试程序
 * @author CameraSubsystem Team
 * @date 2026-01-31
 */

#include "camera_subsystem/broker/frame_broker.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace camera_subsystem;

std::atomic<bool> g_running_(true);

void SignalHandler(int signal)
{
    (void)signal;
    g_running_.store(false);
}

class StressSubscriber : public broker::IFrameSubscriber
{
public:
    explicit StressSubscriber(std::string name, uint8_t priority)
        : name_(std::move(name))
        , priority_(priority)
        , received_count_(0)
    {
    }

    void OnFrame(const core::FrameHandle& /*frame*/) override
    {
        received_count_.fetch_add(1);
    }

    const char* GetSubscriberName() const override
    {
        return name_.c_str();
    }

    uint8_t GetPriority() const override
    {
        return priority_;
    }

    uint64_t GetReceivedCount() const
    {
        return received_count_.load();
    }

private:
    std::string name_;
    uint8_t priority_;
    std::atomic<uint64_t> received_count_;
};

core::FrameHandle BuildTestFrame(uint32_t frame_id)
{
    core::FrameHandle frame;
    frame.Reset();
    frame.frame_id_ = frame_id;
    frame.width_ = 1920;
    frame.height_ = 1080;
    frame.format_ = core::PixelFormat::kNV12;
    frame.plane_count_ = 2;
    frame.line_stride_[0] = 1920;
    frame.line_stride_[1] = 1920;
    frame.plane_offset_[0] = 0;
    frame.plane_offset_[1] = 1920 * 1080;
    frame.plane_size_[0] = 1920 * 1080;
    frame.plane_size_[1] = 1920 * 540;
    frame.buffer_size_ = frame.plane_size_[0] + frame.plane_size_[1];
    frame.memory_type_ = core::MemoryType::kDmaBuf;
    return frame;
}

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    if (!platform::PlatformLogger::Initialize(std::string(), core::LogLevel::kInfo))
    {
        return 1;
    }

    int duration_seconds = 5;
    if (argc > 1)
    {
        duration_seconds = std::max(1, std::atoi(argv[1]));
    }

    const int kSubscriberCount = 8;
    const int kWorkerCount = 4;

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "broker_stress",
                                  "FrameBroker stress test start, duration=%ds, subscribers=%d",
                                  duration_seconds, kSubscriberCount);

    broker::FrameBroker broker;
    broker.SetMaxQueueSize(4096);
    broker.Start(kWorkerCount);

    std::vector<std::shared_ptr<StressSubscriber>> subscribers;
    subscribers.reserve(kSubscriberCount);
    for (int i = 0; i < kSubscriberCount; ++i)
    {
        auto subscriber = std::make_shared<StressSubscriber>(
            "subscriber_" + std::to_string(i),
            static_cast<uint8_t>(128 + i)
        );
        broker.Subscribe(subscriber);
        subscribers.push_back(subscriber);
    }

    uint32_t frame_id = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_report = start_time;

    while (g_running_.load())
    {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= duration_seconds)
        {
            break;
        }

        broker.PublishFrame(BuildTestFrame(frame_id++));

        auto since_report = std::chrono::steady_clock::now() - last_report;
        if (std::chrono::duration_cast<std::chrono::seconds>(since_report).count() >= 1)
        {
            const auto stats = broker.GetStats();
            platform::PlatformLogger::Log(
                core::LogLevel::kInfo,
                "broker_stress",
                "published=%lu dispatched=%lu dropped=%lu queue=%zu",
                stats.published_frames,
                stats.dispatched_tasks,
                stats.dropped_tasks,
                stats.queue_size
            );
            last_report = std::chrono::steady_clock::now();
        }
    }

    broker.Stop();

    uint64_t total_received = 0;
    for (const auto& subscriber : subscribers)
    {
        total_received += subscriber->GetReceivedCount();
    }

    const auto final_stats = broker.GetStats();
    platform::PlatformLogger::Log(
        core::LogLevel::kInfo,
        "broker_stress",
        "Summary: published=%lu dispatched=%lu dropped=%lu received=%lu",
        final_stats.published_frames,
        final_stats.dispatched_tasks,
        final_stats.dropped_tasks,
        total_received
    );

    platform::PlatformLogger::Shutdown();
    return 0;
}
