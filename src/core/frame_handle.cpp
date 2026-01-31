/**
 * @file frame_handle.cpp
 * @brief 帧句柄实现
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/core/frame_handle.h"
#include <cstring>

namespace camera_subsystem
{
namespace core
{

FrameHandle::FrameHandle()
    : frame_id_(0), camera_id_(0), timestamp_ns_(0), width_(0), height_(0),
      format_(PixelFormat::kUnknown), plane_count_(0), memory_type_(MemoryType::kMmap),
      buffer_fd_(-1), virtual_address_(nullptr), buffer_size_(0), sequence_(0), flags_(0)
{
    memset(line_stride_, 0, sizeof(line_stride_));
    memset(plane_offset_, 0, sizeof(plane_offset_));
    memset(plane_size_, 0, sizeof(plane_size_));
    memset(reserved_, 0, sizeof(reserved_));
}

void* FrameHandle::GetPlaneData(uint32_t plane_index) const
{
    if (plane_index >= plane_count_ || !virtual_address_)
    {
        return nullptr;
    }

    return static_cast<uint8_t*>(virtual_address_) + plane_offset_[plane_index];
}

size_t FrameHandle::GetPlaneSize(uint32_t plane_index) const
{
    if (plane_index >= plane_count_)
    {
        return 0;
    }

    return plane_size_[plane_index];
}

bool FrameHandle::IsValid() const
{
    return (width_ > 0 && height_ > 0 && format_ != PixelFormat::kUnknown && plane_count_ > 0 &&
            plane_count_ <= 3 && buffer_size_ > 0 &&
            (virtual_address_ != nullptr || buffer_fd_ >= 0));
}

void FrameHandle::Reset()
{
    frame_id_ = 0;
    camera_id_ = 0;
    timestamp_ns_ = 0;
    width_ = 0;
    height_ = 0;
    format_ = PixelFormat::kUnknown;
    plane_count_ = 0;
    memory_type_ = MemoryType::kMmap;
    buffer_fd_ = -1;
    virtual_address_ = nullptr;
    buffer_size_ = 0;
    sequence_ = 0;
    flags_ = 0;

    memset(line_stride_, 0, sizeof(line_stride_));
    memset(plane_offset_, 0, sizeof(plane_offset_));
    memset(plane_size_, 0, sizeof(plane_size_));
    memset(reserved_, 0, sizeof(reserved_));
}

} // namespace core
} // namespace camera_subsystem
