#include "camera_subsystem/utils/video_device_discovery.h"

#include <gtest/gtest.h>

using camera_subsystem::utils::FormatVideoDeviceDiscoveryForLog;
using camera_subsystem::utils::InspectVideoDevice;
using camera_subsystem::utils::VideoDeviceDiscoveryInfo;

TEST(VideoDeviceDiscoveryTest, MissingDeviceProducesUnknownIdentity)
{
    const auto info = InspectVideoDevice("/dev/camera_subsystem_missing_video_device");

    EXPECT_EQ(info.device_path, "/dev/camera_subsystem_missing_video_device");
    EXPECT_FALSE(info.exists);
    EXPECT_EQ(info.physical_id, "unknown");
}

TEST(VideoDeviceDiscoveryTest, FormatIncludesStableFields)
{
    VideoDeviceDiscoveryInfo info;
    info.device_path = "/dev/video45";
    info.exists = true;
    info.physical_id = "usb:32e6:9221:202509021958";
    info.driver = "uvcvideo";
    info.name = "WebCamera: WebCamera";
    info.bus_info = "usb-0000:00:00.0-1.2";
    info.vendor_id = "32e6";
    info.product_id = "9221";
    info.serial = "202509021958";

    const std::string log = FormatVideoDeviceDiscoveryForLog(info);

    EXPECT_NE(log.find("device=/dev/video45"), std::string::npos);
    EXPECT_NE(log.find("exists=1"), std::string::npos);
    EXPECT_NE(log.find("physical_id=usb:32e6:9221:202509021958"), std::string::npos);
    EXPECT_NE(log.find("driver=uvcvideo"), std::string::npos);
    EXPECT_NE(log.find("serial=202509021958"), std::string::npos);
}
