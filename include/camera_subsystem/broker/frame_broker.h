/**
 * @file frame_broker.h
 * @brief 帧分发中心（FrameBroker）
 * @author CameraSubsystem Team
 * @date 2026-01-31
 */

#ifndef CAMERA_SUBSYSTEM_BROKER_FRAME_BROKER_H
#define CAMERA_SUBSYSTEM_BROKER_FRAME_BROKER_H

#include "camera_subsystem/broker/frame_subscriber.h"
#include "camera_subsystem/core/buffer_guard.h"
#include "camera_subsystem/core/frame_handle.h"
#include "camera_subsystem/core/types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace camera_subsystem {
namespace broker {

/**
 * @brief 帧分发中心
 *
 * 负责管理订阅者，并将帧数据分发给订阅者。
 * 支持多线程调度与优先级队列。
 */
class FrameBroker
{
public:
    /**
     * @brief 分发统计信息
     */
    struct Stats
    {
        uint64_t published_frames = 0;
        uint64_t dispatched_tasks = 0;
        uint64_t dropped_tasks = 0;
        size_t queue_size = 0;
        size_t subscriber_count = 0;
    };

    FrameBroker();
    ~FrameBroker();

    /**
     * @brief 启动分发线程
     * @param worker_count 工作线程数量，0 表示使用硬件并发数
     * @return 成功返回 true
     */
    bool Start(size_t worker_count = 0);

    /**
     * @brief 停止分发线程
     */
    void Stop();

    /**
     * @brief 是否正在运行
     */
    bool IsRunning() const;

    /**
     * @brief 订阅
     */
    bool Subscribe(const std::shared_ptr<IFrameSubscriber>& subscriber);

    /**
     * @brief 取消订阅
     */
    void Unsubscribe(const std::shared_ptr<IFrameSubscriber>& subscriber);

    /**
     * @brief 清空订阅者
     */
    void ClearSubscribers();

    /**
     * @brief 获取订阅者数量
     */
    size_t GetSubscriberCount() const;

    /**
     * @brief 发布帧数据
     */
    void PublishFrame(const core::FrameHandle& frame);
    void PublishFrame(const core::FrameHandle& frame,
                      const std::shared_ptr<core::BufferGuard>& buffer_ref);

    /**
     * @brief 设置最大队列长度
     */
    void SetMaxQueueSize(size_t max_queue_size);

    /**
     * @brief 获取最大队列长度
     */
    size_t GetMaxQueueSize() const;

    /**
     * @brief 获取统计信息
     */
    Stats GetStats() const;

private:
    struct DispatchTask
    {
        core::FrameHandle frame;
        std::shared_ptr<IFrameSubscriber> subscriber;
        std::shared_ptr<core::BufferGuard> buffer_ref; // ARCH-001: 绑定 Buffer 生命周期
        uint8_t priority = 0;
        uint64_t sequence = 0;
    };

    struct TaskCompare
    {
        bool operator()(const DispatchTask& lhs, const DispatchTask& rhs) const
        {
            if (lhs.priority != rhs.priority)
            {
                return lhs.priority < rhs.priority;
            }
            return lhs.sequence > rhs.sequence;
        }
    };

    void WorkerLoop();
    void CleanupExpiredSubscribersLocked();

    mutable std::mutex subscribers_mutex_;
    std::vector<std::weak_ptr<IFrameSubscriber>> subscribers_;

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::priority_queue<DispatchTask, std::vector<DispatchTask>, TaskCompare> task_queue_;

    std::vector<std::thread> workers_;
    std::atomic<bool> is_running_;
    std::atomic<uint64_t> sequence_;
    std::atomic<uint64_t> published_frames_;
    std::atomic<uint64_t> dispatched_tasks_;
    std::atomic<uint64_t> dropped_tasks_;
    std::atomic<size_t> max_queue_size_;
};

} // namespace broker
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_BROKER_FRAME_BROKER_H
