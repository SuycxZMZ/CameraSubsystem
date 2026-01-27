/**
 * @file frame_subscriber.h
 * @brief 帧订阅者接口定义
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_BROKER_FRAME_SUBSCRIBER_H
#define CAMERA_SUBSYSTEM_BROKER_FRAME_SUBSCRIBER_H

#include "../core/frame_handle.h"

namespace camera_subsystem {
namespace broker {

/**
 * @brief 帧订阅者接口
 *
 * 上层应用 (AI、编码等) 需实现此接口以接收帧数据。
 */
class IFrameSubscriber
{
public:
    /**
     * @brief 虚析构函数
     */
    virtual ~IFrameSubscriber() = default;

    /**
     * @brief 帧数据回调接口
     * @param frame 帧句柄,包含 buffer 引用
     *
     * @note 该回调在 Worker 线程中执行,需保证线程安全且快速返回
     * @warning 不要在回调中执行耗时操作,避免阻塞分发线程
     */
    virtual void OnFrame(const core::FrameHandle& frame) = 0;

    /**
     * @brief 获取订阅者名称,用于调试和日志
     * @return 订阅者名称字符串
     */
    virtual const char* GetSubscriberName() const = 0;

    /**
     * @brief 获取订阅者优先级,用于任务调度
     * @return 优先级值 (0-255, 数值越大优先级越高)
     *
     * @note 默认实现返回 128 (中等优先级)
     */
    virtual uint8_t GetPriority() const
    {
        return 128;
    }

    /**
     * @brief 订阅者被移除时的回调
     *
     * @note 可用于清理资源或状态
     */
    virtual void OnUnsubscribed()
    {
        // 默认空实现
    }
};

} // namespace broker
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_BROKER_FRAME_SUBSCRIBER_H
