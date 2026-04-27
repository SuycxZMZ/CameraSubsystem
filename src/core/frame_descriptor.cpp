#include "camera_subsystem/core/frame_descriptor.h"

namespace camera_subsystem {
namespace core {

bool FrameDescriptor::IsValid() const
{
    // 基本字段校验：尺寸、格式、plane 数量、总字节数
    if (width == 0 || height == 0 || pixel_format == PixelFormat::kUnknown ||
        plane_count == 0 || plane_count > kMaxFramePlanes || total_bytes_used == 0)
    {
        return false;
    }

    // DMA-BUF 模式额外校验：fd 必须有效，plane 的 fd_index 必须在范围内
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

    // MMAP/heap 模式：只校验 plane length
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
    // 三部分均有效：描述符、兼容句柄、lease 非空
    return descriptor.IsValid() && handle.IsValid() && lease != nullptr;
}

} // namespace core
} // namespace camera_subsystem
