#include "camera_subsystem/utils/video_device_discovery.h"

#include <gtest/gtest.h>

using camera_subsystem::utils::FormatVideoDeviceDiscoveryForLog;
using camera_subsystem::utils::FindUniqueVideoDeviceByPhysicalId;
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
    info.can_capture = true;
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
    EXPECT_NE(log.find("can_capture=1"), std::string::npos);
    EXPECT_NE(log.find("physical_id=usb:32e6:9221:202509021958"), std::string::npos);
    EXPECT_NE(log.find("driver=uvcvideo"), std::string::npos);
    EXPECT_NE(log.find("serial=202509021958"), std::string::npos);
}

TEST(VideoDeviceDiscoveryTest, FindUniquePhysicalIdMatch)
{
    VideoDeviceDiscoveryInfo first;
    first.device_path = "/dev/video45";
    first.exists = true;
    first.can_capture = true;
    first.physical_id = "usb:32e6:9221:202509021958";

    VideoDeviceDiscoveryInfo second;
    second.device_path = "/dev/video12";
    second.exists = true;
    second.can_capture = true;
    second.physical_id = "usb:abcd:0001:other";

    const auto result = FindUniqueVideoDeviceByPhysicalId({first, second}, first.physical_id);

    EXPECT_TRUE(result.has_unique_match);
    EXPECT_EQ(result.candidate_count, 1U);
    EXPECT_EQ(result.device.device_path, "/dev/video45");
}

TEST(VideoDeviceDiscoveryTest, DuplicatePhysicalIdIsAmbiguous)
{
    VideoDeviceDiscoveryInfo first;
    first.device_path = "/dev/video45";
    first.exists = true;
    first.can_capture = true;
    first.physical_id = "usb:32e6:9221:202509021958";

    VideoDeviceDiscoveryInfo second = first;
    second.device_path = "/dev/video46";

    const auto result = FindUniqueVideoDeviceByPhysicalId({first, second}, first.physical_id);

    EXPECT_FALSE(result.has_unique_match);
    EXPECT_EQ(result.candidate_count, 2U);
    EXPECT_TRUE(result.device.device_path.empty());
}

TEST(VideoDeviceDiscoveryTest, UnknownPhysicalIdNeverMatches)
{
    VideoDeviceDiscoveryInfo device;
    device.device_path = "/dev/video45";
    device.exists = true;
    device.can_capture = true;
    device.physical_id = "unknown";

    const auto result = FindUniqueVideoDeviceByPhysicalId({device}, "unknown");

    EXPECT_FALSE(result.has_unique_match);
    EXPECT_EQ(result.candidate_count, 0U);
}

TEST(VideoDeviceDiscoveryTest, NonCaptureNodeDoesNotMatch)
{
    VideoDeviceDiscoveryInfo capture;
    capture.device_path = "/dev/video45";
    capture.exists = true;
    capture.can_capture = true;
    capture.physical_id = "usb:32e6:9221:202509021958";

    VideoDeviceDiscoveryInfo metadata;
    metadata.device_path = "/dev/video46";
    metadata.exists = true;
    metadata.can_capture = false;
    metadata.physical_id = capture.physical_id;

    const auto result =
        FindUniqueVideoDeviceByPhysicalId({capture, metadata}, capture.physical_id);

    EXPECT_TRUE(result.has_unique_match);
    EXPECT_EQ(result.candidate_count, 1U);
    EXPECT_EQ(result.device.device_path, "/dev/video45");
}
