#include "detection_server/detection_config.h"

#include <gtest/gtest.h>

#include <iterator>

using namespace camera_subsystem::extensions::detection_server;

TEST(DetectionConfigTest, DefaultConfigIsValid)
{
    DetectionServerConfig config;
    std::string reason;

    EXPECT_TRUE(config.IsValid(&reason));
    EXPECT_TRUE(reason.empty());
    EXPECT_EQ(config.stream_id, "default0");
    EXPECT_EQ(config.npu_core_mask, 1U);
    EXPECT_EQ(config.performance_profile, PerformanceProfile::kNpuCpu);
    EXPECT_EQ(config.output_mode, DetectionOutputMode::kMetadataAndAnnotatedFrame);
}

TEST(DetectionConfigTest, RejectsUnsupportedCoreMask)
{
    DetectionServerConfig config;
    std::string reason;
    config.npu_core_mask = 5;

    EXPECT_FALSE(config.IsValid(&reason));
    EXPECT_NE(reason.find("npu_core_mask"), std::string::npos);
}

TEST(DetectionConfigTest, RejectsUnsupportedFullProfile)
{
    DetectionServerConfig config;
    std::string reason;
    config.performance_profile = PerformanceProfile::kFull;

    EXPECT_FALSE(config.IsValid(&reason));
    EXPECT_NE(reason.find("full"), std::string::npos);
}

TEST(DetectionConfigTest, ParseConfigOverridesDefaults)
{
    char arg0[] = "camera_detection_server";
    char arg1[] = "--stream-id";
    char arg2[] = "front_usb";
    char arg3[] = "--npu-core-mask";
    char arg4[] = "4";
    char arg5[] = "--performance-profile";
    char arg6[] = "npu";
    char arg7[] = "--draw-boxes";
    char arg8[] = "0";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8};

    DetectionServerConfig config;
    EXPECT_EQ(ParseDetectionServerConfig(static_cast<int>(std::size(argv)), argv, &config),
              ParseResult::kOk);
    EXPECT_EQ(config.stream_id, "front_usb");
    EXPECT_EQ(config.npu_core_mask, 4U);
    EXPECT_EQ(config.performance_profile, PerformanceProfile::kNpu);
    EXPECT_FALSE(config.draw_boxes);
}

TEST(DetectionConfigTest, ParseConfigRejectsInvalidOutputMode)
{
    char arg0[] = "camera_detection_server";
    char arg1[] = "--output-mode";
    char arg2[] = "bad_mode";
    char* argv[] = {arg0, arg1, arg2};

    DetectionServerConfig config;
    EXPECT_EQ(ParseDetectionServerConfig(static_cast<int>(std::size(argv)), argv, &config),
              ParseResult::kError);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
