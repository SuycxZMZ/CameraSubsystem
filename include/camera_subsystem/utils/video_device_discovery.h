/**
 * @file video_device_discovery.h
 * @brief Video device identity discovery helpers
 * @author CameraSubsystem Team
 * @date 2026-05-16
 */

#ifndef CAMERA_SUBSYSTEM_UTILS_VIDEO_DEVICE_DISCOVERY_H
#define CAMERA_SUBSYSTEM_UTILS_VIDEO_DEVICE_DISCOVERY_H

#include <cstdint>
#include <string>
#include <vector>

namespace camera_subsystem
{
namespace utils
{

struct VideoDeviceDiscoveryInfo
{
    std::string device_path;
    std::string name;
    std::string driver;
    std::string bus_info;
    std::string subsystem;
    std::string vendor_id;
    std::string product_id;
    std::string serial;
    std::string physical_path;
    std::string physical_id;
    uint32_t capabilities = 0;
    uint32_t device_capabilities = 0;
    bool can_capture = false;
    bool exists = false;
};

struct VideoDeviceMatchResult
{
    VideoDeviceDiscoveryInfo device;
    size_t candidate_count = 0;
    bool has_unique_match = false;
};

VideoDeviceDiscoveryInfo InspectVideoDevice(const std::string& device_path);

std::vector<VideoDeviceDiscoveryInfo> ScanVideoDevices();

VideoDeviceMatchResult FindUniqueVideoDeviceByPhysicalId(
    const std::vector<VideoDeviceDiscoveryInfo>& devices,
    const std::string& physical_id);

std::string FormatVideoDeviceDiscoveryForLog(const VideoDeviceDiscoveryInfo& info);

} // namespace utils
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_UTILS_VIDEO_DEVICE_DISCOVERY_H
