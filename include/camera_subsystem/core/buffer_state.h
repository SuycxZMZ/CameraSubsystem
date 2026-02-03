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
 */
enum class BufferState : uint8_t
{
    kFree = 0,
    kInUse,
    kInFlight
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
        default:
            return "Unknown";
    }
}

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_BUFFER_STATE_H
