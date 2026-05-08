/**
 * @file camera_stream_identity.h
 * @brief Stable stream identity for multi-camera routing
 */

#ifndef CAMERA_SUBSYSTEM_CORE_CAMERA_STREAM_IDENTITY_H
#define CAMERA_SUBSYSTEM_CORE_CAMERA_STREAM_IDENTITY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace camera_subsystem
{
namespace core
{

constexpr size_t kCameraStreamIdMaxLength = 64;
constexpr const char* kDefaultCameraStreamId = "default0";

struct CameraStreamIdentity
{
    uint32_t camera_id = 0;
    std::array<char, kCameraStreamIdMaxLength> stream_id{};
    uint32_t bus_type = 0;
    uint32_t bus_index = 0;
};

CameraStreamIdentity MakeDefaultCameraStreamIdentity(uint32_t camera_id = 0);
CameraStreamIdentity MakeCameraStreamIdentity(const std::string& stream_id,
                                              uint32_t camera_id,
                                              uint32_t bus_type = 0,
                                              uint32_t bus_index = 0);

std::string GetCameraStreamId(const CameraStreamIdentity& identity);
void SetCameraStreamId(CameraStreamIdentity* identity, const std::string& stream_id);
bool IsCameraStreamIdentityValid(const CameraStreamIdentity& identity);

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_CAMERA_STREAM_IDENTITY_H
