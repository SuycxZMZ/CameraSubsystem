/**
 * @file frame_handle.h
 * @brief 帧句柄结构定义
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_CORE_FRAME_HANDLE_H
#define CAMERA_SUBSYSTEM_CORE_FRAME_HANDLE_H

#include "types.h"
#include <cstddef>
#include <cstdint>

namespace camera_subsystem
{
namespace core
{

/**
 * @brief 帧句柄结构 (C-Style POD)
 *
 * FrameHandle 是帧数据的唯一凭证,包含所有渲染和处理所需的描述信息。
 * 设计遵循 POD (Plain Old Data) 原则,确保在 C/C++ 边界及共享内存中安全传递。
 *
 * 设计要点:
 * - POD 结构,可在 C/C++ 边界安全传递
 * - 支持多平面格式 (如 NV12)
 * - 包含 Stride 信息,处理对齐和 Padding
 * - 预留扩展空间,便于向后兼容
 */
struct FrameHandle
{
    // --- 基础标识 ---
    uint32_t frame_id_;     // 全局递增帧序号
    uint32_t camera_id_;    // 相机设备 ID
    uint64_t timestamp_ns_; // 纳秒级时间戳 (CLOCK_MONOTONIC)

    // --- 图像属性 ---
    uint32_t width_;     // 图像宽度 (像素)
    uint32_t height_;    // 图像高度 (像素)
    PixelFormat format_; // 像素格式

    // --- 内存布局 (关键: 支持对齐与多平面) ---
    uint32_t plane_count_;     // 平面数量 (1, 2 or 3)
    uint32_t line_stride_[3];  // 每个平面的行跨度 (字节)
    uint32_t plane_offset_[3]; // 每个平面相对于 buffer 起始的偏移
    uint32_t plane_size_[3];   // 每个平面的大小

    // --- 内存句柄 ---
    MemoryType memory_type_; // 内存类型
    int buffer_fd_;          // DMA-BUF 或 Shared Memory FD
    void* virtual_address_;  // 映射后的虚拟地址 (仅 CPU 访问有效)
    size_t buffer_size_;     // Buffer 总大小

    // --- 扩展字段 ---
    uint32_t sequence_;    // 帧序列号 (V4L2)
    uint32_t flags_;       // 标志位 (保留)
    uint8_t reserved_[56]; // 预留扩展空间 (总计 64 字节)

    /**
     * @brief 默认构造函数
     */
    FrameHandle();

    /**
     * @brief 获取指定平面的数据指针
     * @param plane_index 平面索引 (0, 1, 2)
     * @return 平面数据指针,失败返回 nullptr
     */
    void* GetPlaneData(uint32_t plane_index) const;

    /**
     * @brief 获取指定平面的大小
     * @param plane_index 平面索引 (0, 1, 2)
     * @return 平面大小,失败返回 0
     */
    size_t GetPlaneSize(uint32_t plane_index) const;

    /**
     * @brief 检查帧句柄是否有效
     * @return true 表示有效,false 表示无效
     */
    bool IsValid() const;

    /**
     * @brief 重置帧句柄
     */
    void Reset();
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_FRAME_HANDLE_H
