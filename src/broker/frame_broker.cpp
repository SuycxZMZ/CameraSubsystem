#include "camera_subsystem/broker/frame_broker.h"

#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>

namespace camera_subsystem
{
namespace broker
{

FrameBroker::FrameBroker()
    : is_running_(false), sequence_(0), published_frames_(0), dispatched_tasks_(0),
      dropped_tasks_(0), max_queue_size_(1024)
{
}

FrameBroker::~FrameBroker()
{
    Stop();
}

bool FrameBroker::Start(size_t worker_count)
{
    if (is_running_)
    {
        return true;
    }

    if (worker_count == 0)
    {
        worker_count = std::max(1u, std::thread::hardware_concurrency());
    }

    is_running_ = true;
    workers_.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i)
    {
        workers_.emplace_back(&FrameBroker::WorkerLoop, this);
    }

    return true;
}

void FrameBroker::Stop()
{
    if (!is_running_)
    {
        return;
    }

    is_running_ = false;
    queue_cv_.notify_all();

    for (auto& worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    workers_.clear();

    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!task_queue_.empty())
    {
        task_queue_.pop();
    }
}

bool FrameBroker::IsRunning() const
{
    return is_running_.load();
}

bool FrameBroker::Subscribe(const std::shared_ptr<IFrameSubscriber>& subscriber)
{
    if (!subscriber)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(subscribers_mutex_);

    for (const auto& weak_sub : subscribers_)
    {
        auto existing = weak_sub.lock();
        if (existing && existing.get() == subscriber.get())
        {
            return false;
        }
    }

    subscribers_.push_back(subscriber);
    return true;
}

void FrameBroker::Unsubscribe(const std::shared_ptr<IFrameSubscriber>& subscriber)
{
    if (!subscriber)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                      [&](const std::weak_ptr<IFrameSubscriber>& weak_sub)
                                      {
                                          auto existing = weak_sub.lock();
                                          return !existing || existing.get() == subscriber.get();
                                      }),
                       subscribers_.end());
}

void FrameBroker::ClearSubscribers()
{
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    subscribers_.clear();
}

size_t FrameBroker::GetSubscriberCount() const
{
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    size_t count = 0;
    for (const auto& weak_sub : subscribers_)
    {
        if (!weak_sub.expired())
        {
            ++count;
        }
    }
    return count;
}

void FrameBroker::PublishFrame(const core::FrameHandle& frame)
{
    PublishFrame(frame, nullptr);
}

void FrameBroker::PublishFrame(const core::FrameHandle& frame,
                               const std::shared_ptr<core::BufferGuard>& buffer_ref)
{
    if (!is_running_)
    {
        return;
    }

    std::vector<std::shared_ptr<IFrameSubscriber>> subscribers;
    {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        CleanupExpiredSubscribersLocked();

        for (const auto& weak_sub : subscribers_)
        {
            auto sub = weak_sub.lock();
            if (sub)
            {
                subscribers.push_back(sub);
            }
        }
    }

    if (subscribers.empty())
    {
        return;
    }

    std::sort(
        subscribers.begin(), subscribers.end(),
        [](const std::shared_ptr<IFrameSubscriber>& a, const std::shared_ptr<IFrameSubscriber>& b)
        { return a->GetPriority() > b->GetPriority(); });

    if (buffer_ref)
    {
        // ARCH-002: Buffer 从 InUse 进入 InFlight 状态
        buffer_ref->MarkInFlight();
    }

    published_frames_.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (const auto& sub : subscribers)
        {
            const BackpressureConfig config = sub->GetBackpressureConfig();
            const size_t max_size = (config.max_queue_size > 0)
                                        ? config.max_queue_size
                                        : max_queue_size_.load();

            auto& count = subscriber_task_counts_[sub.get()];
            if (count >= max_size)
            {
                dropped_tasks_.fetch_add(1);

                auto& stats = subscriber_stats_[sub.get()];
                stats.name = sub->GetSubscriberName();
                stats.dropped++;
                stats.consecutive_drops++;
                if (stats.consecutive_drops >= config.slow_consumer_threshold &&
                    !stats.is_slow_consumer)
                {
                    stats.is_slow_consumer = true;
                    stats.slow_consumer_detected_count++;
                    platform::PlatformLogger::Log(
                        core::LogLevel::kWarning, "frame_broker",
                        "Slow consumer detected: %s consecutive_drops=%lu",
                        sub->GetSubscriberName(), stats.consecutive_drops);
                }
                continue;
            }

            DispatchTask task;
            task.frame = frame;
            task.subscriber = sub;
            task.buffer_ref = buffer_ref;
            task.priority = sub->GetPriority();
            task.sequence = sequence_.fetch_add(1);

            task_queue_.push(std::move(task));
            count++;

            auto& stats = subscriber_stats_[sub.get()];
            stats.name = sub->GetSubscriberName();
        }
    }

    queue_cv_.notify_all();
}

void FrameBroker::SetMaxQueueSize(size_t max_queue_size)
{
    max_queue_size_.store(max_queue_size);
}

size_t FrameBroker::GetMaxQueueSize() const
{
    return max_queue_size_.load();
}

FrameBroker::Stats FrameBroker::GetStats() const
{
    Stats stats;
    stats.published_frames = published_frames_.load();
    stats.dispatched_tasks = dispatched_tasks_.load();
    stats.dropped_tasks = dropped_tasks_.load();
    stats.subscriber_count = GetSubscriberCount();

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stats.queue_size = task_queue_.size();

        stats.subscriber_stats.reserve(subscriber_stats_.size());
        for (const auto& item : subscriber_stats_)
        {
            stats.subscriber_stats.push_back(item.second);
        }
    }

    return stats;
}

void FrameBroker::WorkerLoop()
{
    while (true)
    {
        DispatchTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [&]() { return !task_queue_.empty() || !is_running_; });

            if (!is_running_ && task_queue_.empty())
            {
                return;
            }

            task = std::move(task_queue_.top());
            task_queue_.pop();
        }

        if (task.subscriber)
        {
            try
            {
                task.subscriber->OnFrame(task.frame);
                dispatched_tasks_.fetch_add(1);

                std::lock_guard<std::mutex> lock(queue_mutex_);
                auto& stats = subscriber_stats_[task.subscriber.get()];
                stats.dispatched++;
                stats.consecutive_drops = 0;
                if (stats.is_slow_consumer)
                {
                    stats.is_slow_consumer = false;
                    platform::PlatformLogger::Log(
                        core::LogLevel::kInfo, "frame_broker",
                        "Slow consumer recovered: %s",
                        task.subscriber->GetSubscriberName());
                }

                auto it = subscriber_task_counts_.find(task.subscriber.get());
                if (it != subscriber_task_counts_.end() && it->second > 0)
                {
                    it->second--;
                }
            }
            catch (const std::exception& e)
            {
                platform::PlatformLogger::Log(core::LogLevel::kError, "frame_broker",
                                              "Subscriber %s threw exception: %s",
                                              task.subscriber->GetSubscriberName(), e.what());

                std::lock_guard<std::mutex> lock(queue_mutex_);
                auto it = subscriber_task_counts_.find(task.subscriber.get());
                if (it != subscriber_task_counts_.end() && it->second > 0)
                {
                    it->second--;
                }
            }
        }
    }
}

void FrameBroker::CleanupExpiredSubscribersLocked()
{
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                      [](const std::weak_ptr<IFrameSubscriber>& weak_sub)
                                      { return weak_sub.expired(); }),
                       subscribers_.end());
}

} // namespace broker
} // namespace camera_subsystem
