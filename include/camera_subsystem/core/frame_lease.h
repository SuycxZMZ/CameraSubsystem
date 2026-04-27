/**
 * @file frame_lease.h
 * @brief 帧生命周期 lease 抽象（heap 与 DMA-BUF 并列管理）
 *
 * FrameLease 是帧底层 buffer 的"租约"抽象，表示某个 V4L2 buffer 暂不可复用。
 * 核心约束：只要 lease 未 release，生产端就不能对该 buffer 执行 QBUF。
 *
 * 两个并列实现：
 * - HeapFrameLease：包装 BufferGuard，用于 MMAP + copy fallback 路径。
 *   Release() 归还 BufferGuard 给 BufferPool。
 * - DmaBufFrameLease：绑定 V4L2 buffer index 和 release callback，
 *   Release() 触发 callback（即 CameraSource 内部的 QBUF 逻辑）。
 *
 * 设计原则：
 * - DmaBufFrameLease 不继承 BufferGuard，两者管理的资源不同。
 * - Release() 必须幂等，多次调用安全。
 * - FrameLease 不可拷贝，通过 shared_ptr 在消费者之间共享。
 */

#ifndef CAMERA_SUBSYSTEM_CORE_FRAME_LEASE_H
#define CAMERA_SUBSYSTEM_CORE_FRAME_LEASE_H

#include "camera_subsystem/core/buffer_guard.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace camera_subsystem {
namespace core {

/**
 * @brief 帧生命周期 lease 抽象基类
 *
 * 表示某个帧的底层 buffer 暂不可复用。消费者通过 shared_ptr<FrameLease>
 * 持有 lease；当所有 shared_ptr 析构时，Release() 被调用，生产端才可 QBUF。
 */
class FrameLease
{
public:
    virtual ~FrameLease() = default;

    FrameLease(const FrameLease&) = delete;
    FrameLease& operator=(const FrameLease&) = delete;

    /// 释放 lease，允许生产端复用底层 buffer。必须幂等。
    virtual void Release() = 0;
    /// @return true 表示 lease 已释放
    virtual bool IsReleased() const = 0;
    /// @return 关联的 V4L2 buffer 索引（或 BufferPool slot 索引）
    virtual uint32_t BufferId() const = 0;

protected:
    FrameLease() = default;
};

/**
 * @brief Heap buffer lease，包装 BufferGuard 用于 MMAP + copy fallback
 *
 * Release() 将 BufferGuard 归还给 BufferPool，使对应 heap slot 可被后续帧复用。
 * 适用于不使用 DMA-BUF 的传统 MMAP 采集路径。
 */
class HeapFrameLease final : public FrameLease
{
public:
    /// @param guard  BufferGuard 的 shared_ptr，持有 BufferPool 中某个 slot 的引用
    explicit HeapFrameLease(std::shared_ptr<BufferGuard> guard);
    ~HeapFrameLease() override;

    void Release() override;
    bool IsReleased() const override;
    uint32_t BufferId() const override;
    /// @return 持有的 BufferGuard（release 前有效，release 后为 nullptr）
    std::shared_ptr<BufferGuard> Guard() const;

private:
    std::shared_ptr<BufferGuard> guard_;  ///< BufferPool slot 引用
    std::atomic<bool> released_{false};    ///< release 状态标记
};

/**
 * @brief DMA-BUF frame lease，绑定 V4L2 buffer index 和 QBUF release callback
 *
 * Release() 调用 release_callback_(buffer_id_)，触发 CameraSource 内部
 * 对该 V4L2 buffer 执行 VIDIOC_QBUF，使其重新进入驱动采集队列。
 *
 * callback 通过 weak_ptr<RequeueContext> 捕获 CameraSource 的 requeue 上下文，
 * 确保 CameraSource 析构后 callback 不会悬空。
 */
class DmaBufFrameLease final : public FrameLease
{
public:
    /// Release 回调类型：参数为 buffer_id，由 CameraSource 提供 QBUF 逻辑
    using ReleaseCallback = std::function<void(uint32_t buffer_id)>;

    /// @param buffer_id        V4L2 buffer 索引
    /// @param release_callback release 时调用的回调（触发 QBUF）
    DmaBufFrameLease(uint32_t buffer_id, ReleaseCallback release_callback);
    ~DmaBufFrameLease() override;

    void Release() override;
    bool IsReleased() const override;
    uint32_t BufferId() const override;

private:
    uint32_t buffer_id_ = 0;                ///< V4L2 buffer 索引
    ReleaseCallback release_callback_;       ///< release 回调（触发 QBUF）
    std::atomic<bool> released_{false};      ///< release 状态标记
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_FRAME_LEASE_H
