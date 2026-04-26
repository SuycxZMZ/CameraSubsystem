#include "camera_subsystem/core/frame_descriptor.h"

namespace camera_subsystem {
namespace core {

bool FrameDescriptor::IsValid() const
{
    if (width == 0 || height == 0 || pixel_format == PixelFormat::kUnknown ||
        plane_count == 0 || plane_count > kMaxFramePlanes || total_bytes_used == 0)
    {
        return false;
    }

    if (memory_type == MemoryType::kDmaBuf)
    {
        if (fd_count == 0 || fd_count > kMaxFrameFds)
        {
            return false;
        }

        for (uint32_t i = 0; i < fd_count; ++i)
        {
            if (fds[i] < 0)
            {
                return false;
            }
        }

        for (uint32_t i = 0; i < plane_count; ++i)
        {
            if (planes[i].fd_index >= fd_count || planes[i].length == 0)
            {
                return false;
            }
        }
        return true;
    }

    for (uint32_t i = 0; i < plane_count; ++i)
    {
        if (planes[i].length == 0)
        {
            return false;
        }
    }
    return true;
}

bool FramePacket::IsValid() const
{
    return descriptor.IsValid() && handle.IsValid() && lease != nullptr;
}

} // namespace core
} // namespace camera_subsystem
