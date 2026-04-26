/**
 * @file frame_descriptor.h
 * @brief FrameDescriptor and FramePacket definitions
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

constexpr uint32_t kMaxFramePlanes = 3;
constexpr uint32_t kMaxFrameFds = 3;
constexpr uint32_t kInvalidFrameFdIndex = UINT32_MAX;

struct FramePlaneDescriptor
{
    uint32_t fd_index = kInvalidFrameFdIndex;
    uint32_t offset = 0;
    uint32_t stride = 0;
    uint32_t length = 0;
    uint32_t bytes_used = 0;
};

struct FrameDescriptor
{
    uint64_t frame_id = 0;
    uint32_t camera_id = 0;
    uint64_t timestamp_ns = 0;
    uint32_t sequence = 0;

    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat pixel_format = PixelFormat::kUnknown;
    MemoryType memory_type = MemoryType::kMmap;

    uint32_t buffer_id = 0;
    uint32_t plane_count = 0;
    uint32_t fd_count = 0;
    std::array<int, kMaxFrameFds> fds{{-1, -1, -1}};
    std::array<FramePlaneDescriptor, kMaxFramePlanes> planes{};
    uint64_t total_bytes_used = 0;
    uint32_t flags = 0;

    bool IsValid() const;
};

struct FramePacket
{
    FrameDescriptor descriptor;
    FrameHandle handle;
    std::shared_ptr<FrameLease> lease;

    bool IsValid() const;
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_FRAME_DESCRIPTOR_H
