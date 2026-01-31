/**
 * @file test_camera_config.cpp
 * @brief CameraConfig 单元测试
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/core/camera_config.h"
#include "camera_subsystem/core/types.h"
#include <gtest/gtest.h>

using namespace camera_subsystem::core;

/**
 * @brief 测试 CameraConfig 默认构造函数
 */
TEST(CameraConfigTest, DefaultConstructor)
{
    CameraConfig config;

    EXPECT_EQ(config.width_, 0);
    EXPECT_EQ(config.height_, 0);
    EXPECT_EQ(config.format_, PixelFormat::kUnknown);
    EXPECT_EQ(config.fps_, 0);
    EXPECT_EQ(config.buffer_count_, 0);
    EXPECT_EQ(config.io_method_, static_cast<uint32_t>(IoMethod::kDmaBuf));
}

/**
 * @brief 测试 CameraConfig 参数构造函数
 */
TEST(CameraConfigTest, ParameterizedConstructor)
{
    CameraConfig config(1920,                                  // width
                        1080,                                  // height
                        PixelFormat::kNV12,                    // format
                        30,                                    // fps
                        4,                                     // buffer_count
                        static_cast<uint32_t>(IoMethod::kMmap) // io_method
    );

    EXPECT_EQ(config.width_, 1920);
    EXPECT_EQ(config.height_, 1080);
    EXPECT_EQ(config.format_, PixelFormat::kNV12);
    EXPECT_EQ(config.fps_, 30);
    EXPECT_EQ(config.buffer_count_, 4);
    EXPECT_EQ(config.io_method_, static_cast<uint32_t>(IoMethod::kMmap));
}

/**
 * @brief 测试 CameraConfig IsValid 方法
 */
TEST(CameraConfigTest, IsValid)
{
    CameraConfig config;

    // 默认构造的配置应该是无效的
    EXPECT_FALSE(config.IsValid());

    // 设置有效值
    config.width_ = 1920;
    config.height_ = 1080;
    config.format_ = PixelFormat::kNV12;
    config.fps_ = 30;
    config.buffer_count_ = 4;
    config.io_method_ = static_cast<uint32_t>(IoMethod::kDmaBuf);

    // 现在应该是有效的
    EXPECT_TRUE(config.IsValid());

    // 测试边界情况
    config.buffer_count_ = 1;
    EXPECT_FALSE(config.IsValid()); // buffer_count 太小

    config.buffer_count_ = 9;
    EXPECT_FALSE(config.IsValid()); // buffer_count 太大

    config.buffer_count_ = 2;
    EXPECT_TRUE(config.IsValid()); // 最小值

    config.buffer_count_ = 8;
    EXPECT_TRUE(config.IsValid()); // 最大值
}

/**
 * @brief 测试 CameraConfig Reset 方法
 */
TEST(CameraConfigTest, Reset)
{
    CameraConfig config(1920, 1080, PixelFormat::kNV12, 30, 4,
                        static_cast<uint32_t>(IoMethod::kMmap));

    EXPECT_EQ(config.width_, 1920);
    EXPECT_EQ(config.height_, 1080);
    EXPECT_EQ(config.format_, PixelFormat::kNV12);
    EXPECT_EQ(config.fps_, 30);
    EXPECT_EQ(config.buffer_count_, 4);

    config.Reset();

    EXPECT_EQ(config.width_, 0);
    EXPECT_EQ(config.height_, 0);
    EXPECT_EQ(config.format_, PixelFormat::kUnknown);
    EXPECT_EQ(config.fps_, 0);
    EXPECT_EQ(config.buffer_count_, 0);
    EXPECT_EQ(config.io_method_, static_cast<uint32_t>(IoMethod::kDmaBuf));
}

/**
 * @brief 测试 CameraConfig GetDefault 方法
 */
TEST(CameraConfigTest, GetDefault)
{
    CameraConfig config = CameraConfig::GetDefault();

    EXPECT_EQ(config.width_, 1920);
    EXPECT_EQ(config.height_, 1080);
    EXPECT_EQ(config.format_, PixelFormat::kNV12);
    EXPECT_EQ(config.fps_, 30);
    EXPECT_EQ(config.buffer_count_, 4);
    EXPECT_EQ(config.io_method_, static_cast<uint32_t>(IoMethod::kDmaBuf));
    EXPECT_TRUE(config.IsValid());
}

/**
 * @brief 测试不同分辨率配置
 */
TEST(CameraConfigTest, DifferentResolutions)
{
    CameraConfig config_720p(1280, 720, PixelFormat::kNV12, 30, 4);
    EXPECT_TRUE(config_720p.IsValid());

    CameraConfig config_1080p(1920, 1080, PixelFormat::kNV12, 30, 4);
    EXPECT_TRUE(config_1080p.IsValid());

    CameraConfig config_4k(3840, 2160, PixelFormat::kNV12, 30, 4);
    EXPECT_TRUE(config_4k.IsValid());
}

/**
 * @brief 测试不同帧率配置
 */
TEST(CameraConfigTest, DifferentFrameRates)
{
    CameraConfig config_15fps(1920, 1080, PixelFormat::kNV12, 15, 4);
    EXPECT_TRUE(config_15fps.IsValid());

    CameraConfig config_30fps(1920, 1080, PixelFormat::kNV12, 30, 4);
    EXPECT_TRUE(config_30fps.IsValid());

    CameraConfig config_60fps(1920, 1080, PixelFormat::kNV12, 60, 4);
    EXPECT_TRUE(config_60fps.IsValid());
}

/**
 * @brief 测试不同像素格式配置
 */
TEST(CameraConfigTest, DifferentFormats)
{
    CameraConfig config_nv12(1920, 1080, PixelFormat::kNV12, 30, 4);
    EXPECT_TRUE(config_nv12.IsValid());

    CameraConfig config_yuyv(1920, 1080, PixelFormat::kYUYV, 30, 4);
    EXPECT_TRUE(config_yuyv.IsValid());

    CameraConfig config_rgb(1920, 1080, PixelFormat::kRGB888, 30, 4);
    EXPECT_TRUE(config_rgb.IsValid());

    CameraConfig config_rgba(1920, 1080, PixelFormat::kRGBA8888, 30, 4);
    EXPECT_TRUE(config_rgba.IsValid());
}

/**
 * @brief 测试不同 IO 方法配置
 */
TEST(CameraConfigTest, DifferentIoMethods)
{
    CameraConfig config_mmap(1920, 1080, PixelFormat::kNV12, 30, 4,
                             static_cast<uint32_t>(IoMethod::kMmap));
    EXPECT_TRUE(config_mmap.IsValid());

    CameraConfig config_dmabuf(1920, 1080, PixelFormat::kNV12, 30, 4,
                               static_cast<uint32_t>(IoMethod::kDmaBuf));
    EXPECT_TRUE(config_dmabuf.IsValid());

    CameraConfig config_userptr(1920, 1080, PixelFormat::kNV12, 30, 4,
                                static_cast<uint32_t>(IoMethod::kUserPtr));
    EXPECT_TRUE(config_userptr.IsValid());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
