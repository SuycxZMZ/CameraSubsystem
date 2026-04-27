/**
 * @file frame_descriptor.h
 * @brief 帧描述符与帧投递单元定义（DMA-BUF Phase 1 核心数据模型）
 *
 * FrameDescriptor 描述一帧的完整元数据：fd、plane 布局、时间戳、格式等。
 * FramePacket 是进程内投递单元，同时携带 FrameDescriptor + FrameHandle + FrameLease，
 * 使消费者既能访问帧元数据，又能通过 lease 控制底层 buffer 的复用时机。
 *
 * 设计约束：
 * - 字段预留多平面（最多 3 plane、3 fd），Phase 1 实现只填单 fd 单平面。
 * - fd 字段只描述可访问句柄，不表达 QBUF 所有权；QBUF 由 FrameLease release 触发。
 * - FramePacket 中的 lease 为 shared_ptr，多个消费者可共享同一 lease；
 *   当所有 shared_ptr 析构后，lease 的 Release() 被调用，触发生产端 QBUF。
 */

#ifndef CAMERA_SUBSYSTEM_CORE_FRAME_DESCRIPTOR_H
#define CAMERA_SUBSYSTEM_CORE_FRAME_DESCRIPTOR_H

#include "camera_subsystem/core/frame_handle.h"
#include "camera_subsystem/core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace camera_subsystem {
namespace core {

class FrameLease;

/// 单帧最多支持的 plane 数量（Y+Cb+Cr 三平面足够覆盖 NV12/NV16/YUYV 等）
constexpr uint32_t kMaxFramePlanes = 3;
/// 单帧最多支持的 DMA-BUF fd 数量（多 plane 可共享同一 fd，以 offset 区分）
constexpr uint32_t kMaxFrameFds = 3;
/// 无效 fd 索引标记，用于 FramePlaneDescriptor::fd_index 初始值
constexpr uint32_t kInvalidFrameFdIndex = UINT32_MAX;

/**
 * @brief 单个 plane 的描述
 *
 * 描述一个 plane 在 DMA-BUF fd 中的偏移、行跨度、分配长度和有效字节数。
 * 多个 plane 可以共享同一个 fd（fd_index 相同），以 offset 区分起始位置。
 */
struct FramePlaneDescriptor
{
    /// 该 plane 对应的 fd 在 FrameDescriptor::fds 数组中的索引
    uint32_t fd_index = kInvalidFrameFdIndex;
    /// plane 在 fd 指向的 DMA-BUF 中的起始偏移（字节）
    uint32_t offset = 0;
    /// 行跨度（字节），即一行像素占用的字节数，包含 padding
    uint32_t stride = 0;
    /// plane 的分配长度（字节），用于 mmap 或 import 时确定映射范围
    uint32_t length = 0;
    /// 当前帧该 plane 的有效字节数，可能小于 length（如 MJPEG 变长）
    uint32_t bytes_used = 0;
};

/**
 * @brief 帧描述符，承载一帧的完整元数据
 *
 * 覆盖 fd、plane 布局、时间戳、格式、buffer 索引等信息。
 * Phase 1 实现只填单 fd 单平面（fd_count=1, plane_count=1），
 * 但字段已预留多平面，避免后续 Phase 3 大改数据结构。
 *
 * 不变量：
 * - fd 字段只描述可访问句柄，不表达 QBUF 所有权
 * - buffer_id 用于生产端标识 V4L2 buffer 索引，消费者不应依赖其值
 * - IsValid() 为 true 时，frame_id、width、height 应为有效值
 */
struct FrameDescriptor
{
    // --- 帧标识与时间 ---
    uint64_t frame_id = 0;       ///< 帧序号，全局递增
    uint32_t camera_id = 0;      ///< 摄像头 ID / 流 ID
    uint64_t timestamp_ns = 0;   ///< 采集时间戳（纳秒，来自 V4L2 DQBUF 或 CLOCK_MONOTONIC）
    uint32_t sequence = 0;       ///< V4L2 sequence 号，驱动填充

    // --- 图像格式 ---
    uint32_t width = 0;          ///< 图像宽度（像素）
    uint32_t height = 0;         ///< 图像高度（像素）
    PixelFormat pixel_format = PixelFormat::kUnknown;  ///< 像素格式
    MemoryType memory_type = MemoryType::kMmap;        ///< 内存类型：kMmap 或 kDmaBuf

    // --- buffer 与 plane 布局 ---
    uint32_t buffer_id = 0;      ///< 生产端 V4L2 buffer 索引，用于 lease release 时定位 QBUF 目标
    uint32_t plane_count = 0;    ///< 有效 plane 数量（1 ~ kMaxFramePlanes）
    uint32_t fd_count = 0;       ///< 有效 fd 数量（1 ~ kMaxFrameFds）
    std::array<int, kMaxFrameFds> fds{{-1, -1, -1}};   ///< DMA-BUF fd 数组，-1 表示无效
    std::array<FramePlaneDescriptor, kMaxFramePlanes> planes{};  ///< plane 描述数组
    uint64_t total_bytes_used = 0;  ///< 所有 plane 的 bytes_used 之和
    uint32_t flags = 0;         ///< 扩展标志位（关键帧、压缩等）

    /// @return true 表示描述符包含有效帧元数据
    bool IsValid() const;
};

/**
 * @brief 帧投递单元，进程内零拷贝传递的完整对象
 *
 * 组合三部分：
 * - FrameDescriptor：帧元数据（fd、plane、格式、时间戳等）
 * - FrameHandle：兼容现有帧句柄，供旧路径消费者使用
 * - shared_ptr<FrameLease>：生命周期控制，lease 析构时触发 V4L2 QBUF
 *
 * 消费者持有 FramePacket 期间，底层 V4L2 buffer 不会被复用；
 * 当消费者释放 FramePacket（shared_ptr 析构），lease 的 Release() 被调用，
 * 生产端才对该 buffer 执行 VIDIOC_QBUF，使其重新进入驱动采集队列。
 */
struct FramePacket
{
    FrameDescriptor descriptor;              ///< 帧元数据描述
    FrameHandle handle;                      ///< 兼容用帧句柄（填充 kDmaBuf + buffer_fd_ 等）
    std::shared_ptr<FrameLease> lease;       ///< 生命周期 lease，release 触发 QBUF

    /// @return true 表示 descriptor 和 lease 均有效
    bool IsValid() const;
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_FRAME_DESCRIPTOR_H
