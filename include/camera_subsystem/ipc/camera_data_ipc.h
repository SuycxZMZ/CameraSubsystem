/**
 * @file camera_data_ipc.h
 * @brief Camera 数据面 IPC 协议定义（示例程序使用）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#ifndef CAMERA_SUBSYSTEM_IPC_CAMERA_DATA_IPC_H
#define CAMERA_SUBSYSTEM_IPC_CAMERA_DATA_IPC_H

#include <cstdint>

namespace camera_subsystem
{
namespace ipc
{

constexpr uint32_t kCameraDataMagic = 0x43444D50;   // "CDMP"
constexpr uint32_t kCameraDataVersion = 1;
constexpr const char* kDefaultCameraDataSocketPath = "/tmp/camera_subsystem_data.sock";

struct CameraDataFrameHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t frame_size;
    uint64_t frame_id;
    uint64_t timestamp_ns;
    uint32_t sequence;
    uint32_t reserved0;
    uint8_t reserved1[32];
};

inline bool IsCameraDataFrameHeaderValid(const CameraDataFrameHeader& header)
{
    return header.magic == kCameraDataMagic &&
           header.version == kCameraDataVersion &&
           header.frame_size > 0;
}

} // namespace ipc
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_IPC_CAMERA_DATA_IPC_H
