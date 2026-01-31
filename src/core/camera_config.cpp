/**
 * @file camera_config.cpp
 * @brief Camera 配置实现
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/core/camera_config.h"
#include <cstring>

namespace camera_subsystem
{
namespace core
{

CameraConfig::CameraConfig()
    : width_(0), height_(0), format_(PixelFormat::kUnknown), fps_(0), buffer_count_(0),
      io_method_(static_cast<uint32_t>(IoMethod::kDmaBuf))
{
    memset(reserved_, 0, sizeof(reserved_));
}

CameraConfig::CameraConfig(uint32_t width, uint32_t height, PixelFormat format, uint32_t fps,
                           uint32_t buffer_count, uint32_t io_method)
    : width_(width), height_(height), format_(format), fps_(fps), buffer_count_(buffer_count),
      io_method_(io_method)
{
    memset(reserved_, 0, sizeof(reserved_));
}

bool CameraConfig::IsValid() const
{
    return (width_ > 0 && height_ > 0 && format_ != PixelFormat::kUnknown && fps_ > 0 &&
            buffer_count_ >= 2 && buffer_count_ <= 8 &&
            io_method_ <= static_cast<uint32_t>(IoMethod::kUserPtr));
}

void CameraConfig::Reset()
{
    width_ = 0;
    height_ = 0;
    format_ = PixelFormat::kUnknown;
    fps_ = 0;
    buffer_count_ = 0;
    io_method_ = static_cast<uint32_t>(IoMethod::kDmaBuf);
    memset(reserved_, 0, sizeof(reserved_));
}

CameraConfig CameraConfig::GetDefault()
{
    return CameraConfig(1920,                                    // width
                        1080,                                    // height
                        PixelFormat::kNV12,                      // format
                        30,                                      // fps
                        4,                                       // buffer_count
                        static_cast<uint32_t>(IoMethod::kDmaBuf) // io_method
    );
}

} // namespace core
} // namespace camera_subsystem
