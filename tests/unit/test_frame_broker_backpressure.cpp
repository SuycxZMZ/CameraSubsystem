/**
 * @file test_frame_broker_backpressure.cpp
 * @brief FrameBroker 背压参数化单元测试
 * @author CameraSubsystem Team
 * @date 2026-05-10
 */

#include <gtest/gtest.h>

#include "camera_subsystem/broker/frame_broker.h"
#include "camera_subsystem/broker/frame_subscriber.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using camera_subsystem::broker::BackpressureConfig;
using camera_subsystem::broker::DropPolicy;
using camera_subsystem::broker::FrameBroker;
using camera_subsystem::broker::IFrameSubscriber;
using camera_subsystem::core::FrameHandle;

namespace
{

class TestSubscriber : public IFrameSubscriber
{
public:
    explicit TestSubscriber(const std::string& name,
                            uint8_t priority = 128,
                            const BackpressureConfig& config = BackpressureConfig{})
        : name_(name)
        , priority_(priority)
        , config_(config)
        , received_count_(0)
    {
    }

    void OnFrame(const FrameHandle&) override
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

    BackpressureConfig GetBackpressureConfig() const override
    {
        return config_;
    }

    uint64_t GetReceivedCount() const
    {
        return received_count_.load();
    }

private:
    std::string name_;
    uint8_t priority_;
    BackpressureConfig config_;
    std::atomic<uint64_t> received_count_{0};
};

FrameHandle BuildTestFrame(uint32_t frame_id)
{
    FrameHandle frame;
    frame.frame_id_ = frame_id;
    frame.width_ = 640;
    frame.height_ = 480;
    return frame;
}

} // namespace

TEST(FrameBrokerBackpressureTest, DefaultConfigUsesGlobalMaxQueueSize)
{
    FrameBroker broker;
    broker.SetMaxQueueSize(4);
    ASSERT_TRUE(broker.Start(1));

    auto sub = std::make_shared<TestSubscriber>("default", 128, BackpressureConfig{});
    ASSERT_TRUE(broker.Subscribe(sub));

    // Publish 6 frames, only 4 should be queued, 2 dropped
    for (uint32_t i = 0; i < 6; ++i)
    {
        broker.PublishFrame(BuildTestFrame(i));
    }

    broker.Stop();

    EXPECT_EQ(sub->GetReceivedCount(), 4u);

    const auto stats = broker.GetStats();
    EXPECT_EQ(stats.dropped_tasks, 2u);
    EXPECT_EQ(stats.dispatched_tasks, 4u);
}

TEST(FrameBrokerBackpressureTest, PerSubscriberMaxQueueSize)
{
    FrameBroker broker;
    broker.SetMaxQueueSize(100); // global default is large
    ASSERT_TRUE(broker.Start(1));

    BackpressureConfig fast_config;
    fast_config.max_queue_size = 2;
    auto fast_sub = std::make_shared<TestSubscriber>("fast", 128, fast_config);

    BackpressureConfig slow_config;
    slow_config.max_queue_size = 5;
    auto slow_sub = std::make_shared<TestSubscriber>("slow", 128, slow_config);

    ASSERT_TRUE(broker.Subscribe(fast_sub));
    ASSERT_TRUE(broker.Subscribe(slow_sub));

    // Publish 20 frames rapidly; worker may process some during publishing
    for (uint32_t i = 0; i < 20; ++i)
    {
        broker.PublishFrame(BuildTestFrame(i));
    }

    broker.Stop();

    // fast sub should have dropped more frames than slow sub
    const auto stats = broker.GetStats();
    ASSERT_EQ(stats.subscriber_stats.size(), 2u);

    uint64_t fast_dropped = 0;
    uint64_t slow_dropped = 0;
    for (const auto& sub_stats : stats.subscriber_stats)
    {
        if (sub_stats.name == "fast")
        {
            fast_dropped = sub_stats.dropped;
        }
        else if (sub_stats.name == "slow")
        {
            slow_dropped = sub_stats.dropped;
        }
    }

    EXPECT_GT(fast_dropped, slow_dropped);
    EXPECT_GT(fast_dropped, 0u);
}

TEST(FrameBrokerBackpressureTest, SlowConsumerDetection)
{
    FrameBroker broker;
    ASSERT_TRUE(broker.Start(1));

    BackpressureConfig config;
    config.max_queue_size = 1;
    config.slow_consumer_threshold = 3;
    auto sub = std::make_shared<TestSubscriber>("slow_consumer", 128, config);
    ASSERT_TRUE(broker.Subscribe(sub));

    // Publish 5 frames, only 1 queued, 4 dropped
    // slow_consumer_detected_count should be >= 1 after exceeding threshold 3
    for (uint32_t i = 0; i < 5; ++i)
    {
        broker.PublishFrame(BuildTestFrame(i));
    }

    broker.Stop();

    const auto stats = broker.GetStats();
    ASSERT_EQ(stats.subscriber_stats.size(), 1u);
    EXPECT_GE(stats.subscriber_stats[0].slow_consumer_detected_count, 1u);
    EXPECT_EQ(stats.subscriber_stats[0].dropped, 4u);
}

TEST(FrameBrokerBackpressureTest, SlowConsumerRecovery)
{
    FrameBroker broker;
    ASSERT_TRUE(broker.Start(1));

    BackpressureConfig config;
    config.max_queue_size = 2;
    config.slow_consumer_threshold = 2;
    auto sub = std::make_shared<TestSubscriber>("recovering", 128, config);
    ASSERT_TRUE(broker.Subscribe(sub));

    // Step 1: Publish 4 frames, 2 queued, 2 dropped -> slow consumer
    for (uint32_t i = 0; i < 4; ++i)
    {
        broker.PublishFrame(BuildTestFrame(i));
    }

    // Wait for worker to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Step 2: Publish 2 more frames, should be queued and processed
    // consecutive_drops should reset to 0 after successful dispatch
    for (uint32_t i = 4; i < 6; ++i)
    {
        broker.PublishFrame(BuildTestFrame(i));
    }

    broker.Stop();

    const auto stats = broker.GetStats();
    ASSERT_EQ(stats.subscriber_stats.size(), 1u);
    EXPECT_EQ(stats.subscriber_stats[0].dispatched, 4u);
    EXPECT_EQ(stats.subscriber_stats[0].consecutive_drops, 0u);
    EXPECT_FALSE(stats.subscriber_stats[0].is_slow_consumer);
}
