/**
 * @file test_metrics_aggregator.cpp
 * @brief MetricsAggregator 与 IMetricsProvider 单元测试
 */

#include "camera_subsystem/core/metrics.h"

#include <gtest/gtest.h>

using camera_subsystem::core::IMetricsProvider;
using camera_subsystem::core::MetricsAggregator;
using camera_subsystem::core::StreamMetrics;

namespace
{

class CaptureMetricsProvider : public IMetricsProvider
{
public:
    explicit CaptureMetricsProvider(uint64_t capture_count = 0, uint64_t dropped_count = 0)
        : capture_count_(capture_count), dropped_count_(dropped_count)
    {
    }

    void FillMetrics(StreamMetrics* metrics) const override
    {
        if (!metrics)
        {
            return;
        }
        metrics->capture_frame_count = capture_count_;
        metrics->capture_dropped_count = dropped_count_;
    }

    void SetCaptureCount(uint64_t count) { capture_count_ = count; }

private:
    uint64_t capture_count_;
    uint64_t dropped_count_;
};

class BrokerMetricsProvider : public IMetricsProvider
{
public:
    explicit BrokerMetricsProvider(uint64_t published_count = 0) : published_count_(published_count) {}

    void FillMetrics(StreamMetrics* metrics) const override
    {
        if (!metrics)
        {
            return;
        }
        metrics->broker_published_count = published_count_;
    }

private:
    uint64_t published_count_;
};

} // namespace

TEST(MetricsAggregatorTest, EmptyAggregatorReturnsDefaultMetrics)
{
    MetricsAggregator aggregator;
    const auto metrics = aggregator.GetStreamMetrics("stream0");

    EXPECT_EQ(metrics.stream_id, "stream0");
    EXPECT_EQ(metrics.capture_frame_count, 0U);
    EXPECT_EQ(metrics.broker_published_count, 0U);
    EXPECT_GT(metrics.timestamp_ns, 0U);
}

TEST(MetricsAggregatorTest, SingleProviderFillsMetrics)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider provider(100, 5);

    aggregator.RegisterProvider("stream0", &provider);
    const auto metrics = aggregator.GetStreamMetrics("stream0");

    EXPECT_EQ(metrics.capture_frame_count, 100U);
    EXPECT_EQ(metrics.capture_dropped_count, 5U);
    EXPECT_EQ(metrics.broker_published_count, 0U);
}

TEST(MetricsAggregatorTest, MultipleProvidersMergeFields)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider source_provider(100, 5);
    BrokerMetricsProvider broker_provider(200);

    aggregator.RegisterProvider("stream0", &source_provider);
    aggregator.RegisterProvider("stream0", &broker_provider);
    const auto metrics = aggregator.GetStreamMetrics("stream0");

    EXPECT_EQ(metrics.capture_frame_count, 100U);
    EXPECT_EQ(metrics.capture_dropped_count, 5U);
    EXPECT_EQ(metrics.broker_published_count, 200U);
}

TEST(MetricsAggregatorTest, MultipleStreamsIsolated)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider provider_a(100);
    CaptureMetricsProvider provider_b(200);

    aggregator.RegisterProvider("stream_a", &provider_a);
    aggregator.RegisterProvider("stream_b", &provider_b);

    const auto metrics_a = aggregator.GetStreamMetrics("stream_a");
    const auto metrics_b = aggregator.GetStreamMetrics("stream_b");

    EXPECT_EQ(metrics_a.capture_frame_count, 100U);
    EXPECT_EQ(metrics_b.capture_frame_count, 200U);
}

TEST(MetricsAggregatorTest, GetAllStreamMetrics)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider provider_a(100);
    CaptureMetricsProvider provider_b(200);

    aggregator.RegisterProvider("stream_a", &provider_a);
    aggregator.RegisterProvider("stream_b", &provider_b);

    const auto all = aggregator.GetAllStreamMetrics();
    EXPECT_EQ(all.size(), 2U);
}

TEST(MetricsAggregatorTest, UnregisterProvider)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider provider(100);

    aggregator.RegisterProvider("stream0", &provider);
    EXPECT_EQ(aggregator.GetStreamMetrics("stream0").capture_frame_count, 100U);

    aggregator.UnregisterProvider("stream0", &provider);
    EXPECT_EQ(aggregator.GetStreamMetrics("stream0").capture_frame_count, 0U);
}

TEST(MetricsAggregatorTest, UnregisterProviderWithMultipleProviders)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider source_provider(100, 5);
    BrokerMetricsProvider broker_provider(200);

    aggregator.RegisterProvider("stream0", &source_provider);
    aggregator.RegisterProvider("stream0", &broker_provider);

    aggregator.UnregisterProvider("stream0", &source_provider);
    const auto metrics = aggregator.GetStreamMetrics("stream0");

    EXPECT_EQ(metrics.capture_frame_count, 0U);
    EXPECT_EQ(metrics.broker_published_count, 200U);
}

TEST(MetricsAggregatorTest, NullProviderIsIgnored)
{
    MetricsAggregator aggregator;
    aggregator.RegisterProvider("stream0", nullptr);
    const auto metrics = aggregator.GetStreamMetrics("stream0");
    EXPECT_EQ(metrics.capture_frame_count, 0U);
}

TEST(MetricsAggregatorTest, FormatForLogContainsExpectedFields)
{
    StreamMetrics metrics;
    metrics.stream_id = "usb0";
    metrics.capture_frame_count = 1848;
    metrics.capture_dropped_count = 0;
    metrics.dma_buf_frame_count = 1848;
    metrics.active_lease_count = 0;
    metrics.lease_exhausted_count = 0;
    metrics.broker_queue_depth = 0;
    metrics.broker_subscriber_count = 2;
    metrics.v2_sent_frame_count = 3693;
    metrics.v2_send_failure_count = 0;
    metrics.release_pending_count = 0;
    metrics.release_timeout_count = 0;
    metrics.release_reclaimed_count = 0;
    metrics.source_degraded = true;
    metrics.source_requested_fps = 30;
    metrics.source_current_target_fps = 15;
    metrics.source_degradation_count = 2;
    metrics.source_degradation_recovery_count = 1;
    metrics.source_degradation_failure_count = 1;

    const std::string log = MetricsAggregator::FormatForLog(metrics);

    EXPECT_NE(log.find("stream=usb0"), std::string::npos);
    EXPECT_NE(log.find("capture=1848"), std::string::npos);
    EXPECT_NE(log.find("subscribers=2"), std::string::npos);
    EXPECT_NE(log.find("v2_sent=3693"), std::string::npos);
    EXPECT_NE(log.find("degraded=1"), std::string::npos);
    EXPECT_NE(log.find("req_fps=30"), std::string::npos);
    EXPECT_NE(log.find("cur_fps=15"), std::string::npos);
    EXPECT_NE(log.find("degrade=2"), std::string::npos);
    EXPECT_NE(log.find("degrade_rec=1"), std::string::npos);
    EXPECT_NE(log.find("degrade_fail=1"), std::string::npos);
}

TEST(MetricsAggregatorTest, ProviderUpdatesReflectedInSubsequentSnapshots)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider provider(100);

    aggregator.RegisterProvider("stream0", &provider);
    EXPECT_EQ(aggregator.GetStreamMetrics("stream0").capture_frame_count, 100U);

    provider.SetCaptureCount(200);
    EXPECT_EQ(aggregator.GetStreamMetrics("stream0").capture_frame_count, 200U);
}

TEST(MetricsAggregatorTest, ProviderUnregisterRemovesData)
{
    MetricsAggregator aggregator;
    CaptureMetricsProvider source_provider(100, 5);
    BrokerMetricsProvider broker_provider(200);

    aggregator.RegisterProvider("stream0", &source_provider);
    aggregator.RegisterProvider("stream0", &broker_provider);

    {
        const auto metrics = aggregator.GetStreamMetrics("stream0");
        EXPECT_EQ(metrics.capture_frame_count, 100U);
        EXPECT_EQ(metrics.capture_dropped_count, 5U);
        EXPECT_EQ(metrics.broker_published_count, 200U);
    }

    aggregator.UnregisterProvider("stream0", &source_provider);
    {
        const auto metrics = aggregator.GetStreamMetrics("stream0");
        EXPECT_EQ(metrics.capture_frame_count, 0U);
        EXPECT_EQ(metrics.capture_dropped_count, 0U);
        EXPECT_EQ(metrics.broker_published_count, 200U);
    }

    aggregator.UnregisterProvider("stream0", &broker_provider);
    {
        const auto metrics = aggregator.GetStreamMetrics("stream0");
        EXPECT_EQ(metrics.capture_frame_count, 0U);
        EXPECT_EQ(metrics.broker_published_count, 0U);
    }
}
