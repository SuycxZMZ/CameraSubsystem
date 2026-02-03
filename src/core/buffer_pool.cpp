/**
 * @file buffer_pool.cpp
 * @brief BufferPool 实现
 * @author CameraSubsystem Team
 * @date 2026-02-02
 */

#include "camera_subsystem/core/buffer_pool.h"
#include "camera_subsystem/core/buffer_guard.h"

#include <algorithm>
#include <cstdio>

namespace camera_subsystem {
namespace core {

BufferPool::BufferPool()
    : buffer_size_(0)
    , initialized_(false)
{
}

BufferPool::~BufferPool()
{
    Clear();
}

bool BufferPool::Initialize(size_t buffer_count, size_t buffer_size)
{
    if (buffer_count == 0 || buffer_size == 0)
    {
        return false;
    }

    Clear();

    buffers_.resize(buffer_count);
    for (size_t i = 0; i < buffer_count; ++i)
    {
        buffers_[i].storage = std::make_unique<uint8_t[]>(buffer_size);
        buffers_[i].block.id = static_cast<uint32_t>(i);
        buffers_[i].block.data = buffers_[i].storage.get();
        buffers_[i].block.size = buffer_size;
        buffers_[i].state = BufferState::kFree;
        free_ids_.push(static_cast<uint32_t>(i));
    }

    buffer_size_ = buffer_size;
    initialized_ = true;

    stats_ = Stats{};
    stats_.total = buffer_count;
    stats_.available = buffer_count;
    stats_.in_use = 0;
    stats_.in_flight = 0;
    stats_.max_in_use = 0;
    stats_.max_in_flight = 0;

    return true;
}

std::shared_ptr<BufferGuard> BufferPool::Acquire()
{
    std::lock_guard<std::mutex> lock(mutex_);

    stats_.acquire_count++;
    if (!initialized_ || free_ids_.empty())
    {
        stats_.acquire_fail++;
        return nullptr;
    }

    const uint32_t id = free_ids_.front();
    free_ids_.pop();

    stats_.available = free_ids_.size();
    stats_.in_use++;
    stats_.max_in_use = std::max(stats_.max_in_use, stats_.in_use);

    buffers_[id].state = BufferState::kInUse;

    return std::shared_ptr<BufferGuard>(new BufferGuard(this, id, &buffers_[id].block));
}

void BufferPool::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto leaks = CollectLeaksLocked();
    if (!leaks.empty())
    {
        std::fprintf(stderr, "BufferPool leak detected: %zu buffers still in use\n",
                     leaks.size());
    }

    while (!free_ids_.empty())
    {
        free_ids_.pop();
    }

    buffers_.clear();
    buffer_size_ = 0;
    initialized_ = false;
    stats_ = Stats{};
}

BufferPool::Stats BufferPool::GetStats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats = stats_;
    stats.available = free_ids_.size();
    return stats;
}

std::vector<uint32_t> BufferPool::CheckLeaks() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return CollectLeaksLocked();
}

std::vector<uint32_t> BufferPool::CollectLeaksLocked() const
{
    std::vector<uint32_t> leaked;
    for (const auto& entry : buffers_)
    {
        if (entry.state != BufferState::kFree)
        {
            leaked.push_back(entry.block.id);
        }
    }
    return leaked;
}

size_t BufferPool::GetBufferCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.size();
}

size_t BufferPool::GetBufferSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_size_;
}

void BufferPool::ReleaseInternal(uint32_t buffer_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || buffer_id >= buffers_.size())
    {
        return;
    }

    if (buffers_[buffer_id].state == BufferState::kInFlight)
    {
        if (stats_.in_flight > 0)
        {
            stats_.in_flight--;
        }
    }
    else if (buffers_[buffer_id].state == BufferState::kInUse)
    {
        if (stats_.in_use > 0)
        {
            stats_.in_use--;
        }
    }

    buffers_[buffer_id].state = BufferState::kFree;
    free_ids_.push(buffer_id);
    stats_.release_count++;
    stats_.available = free_ids_.size();
}

void BufferPool::MarkInFlight(uint32_t buffer_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || buffer_id >= buffers_.size())
    {
        return;
    }

    if (buffers_[buffer_id].state == BufferState::kInUse)
    {
        buffers_[buffer_id].state = BufferState::kInFlight;
        if (stats_.in_use > 0)
        {
            stats_.in_use--;
        }
        stats_.in_flight++;
        stats_.max_in_flight = std::max(stats_.max_in_flight, stats_.in_flight);
    }
}

} // namespace core
} // namespace camera_subsystem
