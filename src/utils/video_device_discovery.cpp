#include "camera_subsystem/utils/video_device_discovery.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace camera_subsystem
{
namespace utils
{
namespace
{

std::string TrimLine(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                              value.back() == ' ' || value.back() == '\t'))
    {
        value.pop_back();
    }
    return value;
}

std::string ReadFirstLine(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
    {
        return {};
    }

    std::string line;
    std::getline(input, line);
    return TrimLine(line);
}

std::string Basename(const std::string& path)
{
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos)
    {
        return path;
    }
    return path.substr(pos + 1);
}

std::string Dirname(const std::string& path)
{
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0)
    {
        return "/";
    }
    return path.substr(0, pos);
}

std::string ResolvePath(const std::string& path)
{
    std::array<char, 4096> buffer{};
    if (realpath(path.c_str(), buffer.data()) == nullptr)
    {
        return {};
    }
    return buffer.data();
}

std::string ResolveLinkBasename(const std::string& path)
{
    const std::string resolved = ResolvePath(path);
    if (resolved.empty())
    {
        return {};
    }
    return Basename(resolved);
}

std::string FindParentFile(const std::string& start_path, const std::string& file_name)
{
    std::string current = start_path;
    for (int depth = 0; depth < 16 && !current.empty() && current != "/"; ++depth)
    {
        const std::string value = ReadFirstLine(current + "/" + file_name);
        if (!value.empty())
        {
            return value;
        }
        current = Dirname(current);
    }
    return {};
}

void FillQueryCapInfo(const std::string& device_path, VideoDeviceDiscoveryInfo* info)
{
    const int fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
    if (fd < 0)
    {
        return;
    }

    v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
    {
        if (info->driver.empty())
        {
            info->driver = reinterpret_cast<const char*>(cap.driver);
        }
        if (info->name.empty())
        {
            info->name = reinterpret_cast<const char*>(cap.card);
        }
        info->bus_info = reinterpret_cast<const char*>(cap.bus_info);
        info->capabilities = cap.capabilities;
        info->device_capabilities = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
                                        ? cap.device_caps
                                        : cap.capabilities;
        info->can_capture =
            (info->device_capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0 ||
            (info->device_capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    }

    close(fd);
}

std::string BuildPhysicalId(const VideoDeviceDiscoveryInfo& info)
{
    if (!info.vendor_id.empty() && !info.product_id.empty() && !info.serial.empty())
    {
        return "usb:" + info.vendor_id + ":" + info.product_id + ":" + info.serial;
    }
    if (!info.vendor_id.empty() && !info.product_id.empty() && !info.bus_info.empty())
    {
        return "usb:" + info.vendor_id + ":" + info.product_id + ":" + info.bus_info;
    }
    if (!info.subsystem.empty() && !info.bus_info.empty())
    {
        return info.subsystem + ":" + info.bus_info;
    }
    if (!info.physical_path.empty())
    {
        return "path:" + info.physical_path;
    }
    return "unknown";
}

} // namespace

VideoDeviceDiscoveryInfo InspectVideoDevice(const std::string& device_path)
{
    VideoDeviceDiscoveryInfo info;
    info.device_path = device_path;

    const std::string video_name = Basename(device_path);
    const std::string sysfs_node = "/sys/class/video4linux/" + video_name;
    const std::string device_node = sysfs_node + "/device";

    info.name = ReadFirstLine(sysfs_node + "/name");
    info.physical_path = ResolvePath(device_node);
    info.exists = !info.physical_path.empty();

    if (info.exists)
    {
        info.driver = ResolveLinkBasename(device_node + "/driver");
        info.subsystem = ResolveLinkBasename(device_node + "/subsystem");
        info.vendor_id = FindParentFile(info.physical_path, "idVendor");
        info.product_id = FindParentFile(info.physical_path, "idProduct");
        info.serial = FindParentFile(info.physical_path, "serial");
    }

    FillQueryCapInfo(device_path, &info);
    info.physical_id = BuildPhysicalId(info);
    return info;
}

std::vector<VideoDeviceDiscoveryInfo> ScanVideoDevices()
{
    std::vector<std::string> device_paths;

    DIR* dir = opendir("/sys/class/video4linux");
    if (!dir)
    {
        return {};
    }

    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name.size() <= 5 || name.compare(0, 5, "video") != 0)
        {
            continue;
        }
        device_paths.push_back("/dev/" + name);
    }
    closedir(dir);

    std::sort(device_paths.begin(), device_paths.end());

    std::vector<VideoDeviceDiscoveryInfo> devices;
    devices.reserve(device_paths.size());
    for (const auto& path : device_paths)
    {
        devices.push_back(InspectVideoDevice(path));
    }
    return devices;
}

VideoDeviceMatchResult FindUniqueVideoDeviceByPhysicalId(
    const std::vector<VideoDeviceDiscoveryInfo>& devices,
    const std::string& physical_id)
{
    VideoDeviceMatchResult result;
    if (physical_id.empty() || physical_id == "unknown")
    {
        return result;
    }

    for (const auto& device : devices)
    {
        if (!device.exists || !device.can_capture || device.physical_id != physical_id)
        {
            continue;
        }

        ++result.candidate_count;
        if (result.candidate_count == 1)
        {
            result.device = device;
        }
    }

    result.has_unique_match = result.candidate_count == 1;
    if (!result.has_unique_match)
    {
        result.device = VideoDeviceDiscoveryInfo{};
    }
    return result;
}

std::string FormatVideoDeviceDiscoveryForLog(const VideoDeviceDiscoveryInfo& info)
{
    std::ostringstream output;
    output << "device=" << info.device_path << " exists=" << (info.exists ? 1 : 0)
           << " can_capture=" << (info.can_capture ? 1 : 0)
           << " physical_id=" << info.physical_id << " driver=" << info.driver
           << " name=" << info.name << " bus_info=" << info.bus_info
           << " subsystem=" << info.subsystem << " vendor_id=" << info.vendor_id
           << " product_id=" << info.product_id << " serial=" << info.serial
           << " physical_path=" << info.physical_path;
    return output.str();
}

} // namespace utils
} // namespace camera_subsystem
