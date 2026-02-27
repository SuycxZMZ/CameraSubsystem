/**
 * @file frame_handle_ex.h
 * @brief FrameHandleEx 扩展结构，绑定 Buffer 生命周期
 * @author CameraSubsystem Team
 * @date 2026-02-27
 * 
 * 解决 ARCH-001: FrameHandle 中的裸指针风险
 */

#ifndef CAMERA_SUBSYSTEM_CORE_FRAME_HANDLE_EX_H
#define CAMERA_SUBSYSTEM_CORE_FRAME_HANDLE_EX_H

#include "camera_subsystem/core/buffer_guard.h"
#include "camera_subsystem/core/frame_handle.h"

#include <memory>

namespace camera_subsystem
{
namespace core
{

/**
 * @brief FrameHandleEx 扩展结构（C++ only）
 * 
 * 持有 shared_ptr<BufferGuard>，绑定 Buffer 生命周期。
 * 解决 FrameHandle 中裸指针的悬空指针风险。
 * 
 * 使用场景：
 * - CameraSource 内部使用
 * - FrameBroker 分发时使用
 * - 订阅者处理时使用
 * 
 * 注意：
 * - FrameHandleEx 不能跨语言边界传递
 * - FrameHandleEx 不能用于共享内存
 */
struct FrameHandleEx
{
    FrameHandle handle;                       // POD 部分
    std::shared_ptr<BufferGuard> buffer_ref;  // Buffer 生命周期绑定

    /**
     * @brief 默认构造函数
     */
    FrameHandleEx() = default;

    /**
     * @brief 构造函数
     * @param frame POD 帧句柄
     * @param ref Buffer 引用
     */
    FrameHandleEx(const FrameHandle& frame, const std::shared_ptr<BufferGuard>& ref)
        : handle(frame), buffer_ref(ref)
    {
    }

    /**
     * @brief 检查是否有效
     * @return true 表示有效
     */
    bool IsValid() const
    {
        return handle.IsValid() && buffer_ref && buffer_ref->IsValid();
    }

    /**
     * @brief 获取数据指针
     * @return 数据指针，失败返回 nullptr
     */
    void* GetData() const
    {
        if (buffer_ref && buffer_ref->IsValid())
        {
            return buffer_ref->Data();
        }
        return nullptr;
    }

    /**
     * @brief 获取数据大小
     * @return 数据大小，失败返回 0
     */
    size_t GetSize() const
    {
        if (buffer_ref && buffer_ref->IsValid())
        {
            return buffer_ref->Size();
        }
        return 0;
    }

    /**
     * @brief 重置
     */
    void Reset()
    {
        handle.Reset();
        buffer_ref.reset();
    }
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_FRAME_HANDLE_EX_H
