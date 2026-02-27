/**
 * @file buffer_state.h
 * @brief Buffer 状态定义
 * @author CameraSubsystem Team
 * @date 2026-02-03
 */

#ifndef CAMERA_SUBSYSTEM_CORE_BUFFER_STATE_H
#define CAMERA_SUBSYSTEM_CORE_BUFFER_STATE_H

#include <cstdint>

namespace camera_subsystem {
namespace core {

/**
 * @brief Buffer 生命周期状态（ARCH-002）
 * 
 * 状态转换：
 * Free -> InUse -> InFlight -> Free
 *   ↑        ↓         ↓
 *   └────────┴─────────┘
 * 
 * 特殊转换：
 * InFlight -> InUse (取消分发)
 * Any -> Error (错误状态)
 */
enum class BufferState : uint8_t
{
    kFree = 0,     // 空闲，可分配
    kInUse,        // 使用中，被 CameraSource 持有
    kInFlight,     // 飞行中，正在分发给订阅者
    kError         // 错误状态，Buffer 损坏
};

inline const char* BufferStateToString(BufferState state)
{
    switch (state)
    {
        case BufferState::kFree:
            return "Free";
        case BufferState::kInUse:
            return "InUse";
        case BufferState::kInFlight:
            return "InFlight";
        case BufferState::kError:
            return "Error";
        default:
            return "Unknown";
    }
}

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_BUFFER_STATE_H
