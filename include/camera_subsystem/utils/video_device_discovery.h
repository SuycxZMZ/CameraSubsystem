/**
 * @file video_device_discovery.h
 * @brief Video device identity discovery helpers
 * @author CameraSubsystem Team
 * @date 2026-05-16
 */

#ifndef CAMERA_SUBSYSTEM_UTILS_VIDEO_DEVICE_DISCOVERY_H
#define CAMERA_SUBSYSTEM_UTILS_VIDEO_DEVICE_DISCOVERY_H

#include <string>

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
    bool exists = false;
};

VideoDeviceDiscoveryInfo InspectVideoDevice(const std::string& device_path);

std::string FormatVideoDeviceDiscoveryForLog(const VideoDeviceDiscoveryInfo& info);

} // namespace utils
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_UTILS_VIDEO_DEVICE_DISCOVERY_H
