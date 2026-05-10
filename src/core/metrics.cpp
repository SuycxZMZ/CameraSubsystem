/**
 * @file metrics.cpp
 * @brief MetricsAggregator 实现
 */

#include "camera_subsystem/core/metrics.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

namespace camera_subsystem
{
namespace core
{

namespace
{

uint64_t GetTimestampNs()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch())
            .count());
}

} // namespace

void MetricsAggregator::RegisterProvider(const std::string& stream_id,
                                         IMetricsProvider* provider)
{
    if (!provider)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    providers_by_stream_[stream_id].push_back(provider);
}

void MetricsAggregator::UnregisterProvider(const std::string& stream_id,
                                           IMetricsProvider* provider)
{
    if (!provider)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = providers_by_stream_.find(stream_id);
    if (it == providers_by_stream_.end())
    {
        return;
    }

    auto& providers = it->second;
    providers.erase(std::remove(providers.begin(), providers.end(), provider), providers.end());

    if (providers.empty())
    {
        providers_by_stream_.erase(it);
    }
}

StreamMetrics MetricsAggregator::GetStreamMetrics(const std::string& stream_id) const
{
    StreamMetrics metrics;
    metrics.stream_id = stream_id;
    metrics.timestamp_ns = GetTimestampNs();

    std::vector<IMetricsProvider*> providers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = providers_by_stream_.find(stream_id);
        if (it != providers_by_stream_.end())
        {
            providers = it->second;
        }
    }

    for (auto* provider : providers)
    {
        if (provider)
        {
            provider->FillMetrics(&metrics);
        }
    }

    return metrics;
}

std::vector<StreamMetrics> MetricsAggregator::GetAllStreamMetrics() const
{
    std::vector<std::string> stream_ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_ids.reserve(providers_by_stream_.size());
        for (const auto& item : providers_by_stream_)
        {
            stream_ids.push_back(item.first);
        }
    }

    std::vector<StreamMetrics> result;
    result.reserve(stream_ids.size());
    for (const auto& stream_id : stream_ids)
    {
        result.push_back(GetStreamMetrics(stream_id));
    }

    return result;
}

std::string MetricsAggregator::FormatForLog(const StreamMetrics& metrics)
{
    char buffer[512];
    int n = std::snprintf(
        buffer, sizeof(buffer),
        "stream=%s | capture=%" PRIu64 " | dropped=%" PRIu64 " | dmabuf=%" PRIu64
        " | leases=%zu | lease_exhausted=%" PRIu64 " | queue=%zu | subscribers=%zu"
        " | v2_sent=%" PRIu64 " | v2_fail=%" PRIu64
        " | release_pending=%" PRIu64 " | release_timeout=%" PRIu64
        " | release_reclaimed=%" PRIu64,
        metrics.stream_id.c_str(),
        metrics.capture_frame_count,
        metrics.capture_dropped_count,
        metrics.dma_buf_frame_count,
        metrics.active_lease_count,
        metrics.lease_exhausted_count,
        metrics.broker_queue_depth,
        metrics.broker_subscriber_count,
        metrics.v2_sent_frame_count,
        metrics.v2_send_failure_count,
        metrics.release_pending_count,
        metrics.release_timeout_count,
        metrics.release_reclaimed_count);

    if (n < 0 || static_cast<size_t>(n) >= sizeof(buffer))
    {
        return std::string(buffer);
    }

    return std::string(buffer, static_cast<size_t>(n));
}

} // namespace core
} // namespace camera_subsystem
