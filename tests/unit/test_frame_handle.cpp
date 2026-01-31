/**
 * @file test_frame_handle.cpp
 * @brief FrameHandle 单元测试
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/core/frame_handle.h"
#include "camera_subsystem/core/types.h"
#include <gtest/gtest.h>

using namespace camera_subsystem::core;

/**
 * @brief 测试 FrameHandle 默认构造函数
 */
TEST(FrameHandleTest, DefaultConstructor)
{
    FrameHandle frame;

    EXPECT_EQ(frame.frame_id_, 0);
    EXPECT_EQ(frame.camera_id_, 0);
    EXPECT_EQ(frame.timestamp_ns_, 0);
    EXPECT_EQ(frame.width_, 0);
    EXPECT_EQ(frame.height_, 0);
    EXPECT_EQ(frame.format_, PixelFormat::kUnknown);
    EXPECT_EQ(frame.plane_count_, 0);
    EXPECT_EQ(frame.memory_type_, MemoryType::kMmap);
    EXPECT_EQ(frame.buffer_fd_, -1);
    EXPECT_EQ(frame.virtual_address_, nullptr);
    EXPECT_EQ(frame.buffer_size_, 0);
}

/**
 * @brief 测试 FrameHandle Reset 方法
 */
TEST(FrameHandleTest, Reset)
{
    FrameHandle frame;
    frame.frame_id_ = 100;
    frame.camera_id_ = 1;
    frame.width_ = 1920;
    frame.height_ = 1080;
    frame.format_ = PixelFormat::kNV12;
    frame.plane_count_ = 2;
    frame.virtual_address_ = reinterpret_cast<void*>(0x1000);
    frame.buffer_size_ = 3110400;

    frame.Reset();

    EXPECT_EQ(frame.frame_id_, 0);
    EXPECT_EQ(frame.camera_id_, 0);
    EXPECT_EQ(frame.width_, 0);
    EXPECT_EQ(frame.height_, 0);
    EXPECT_EQ(frame.format_, PixelFormat::kUnknown);
    EXPECT_EQ(frame.plane_count_, 0);
    EXPECT_EQ(frame.virtual_address_, nullptr);
    EXPECT_EQ(frame.buffer_size_, 0);
}

/**
 * @brief 测试 FrameHandle IsValid 方法
 */
TEST(FrameHandleTest, IsValid)
{
    FrameHandle frame;

    // 默认构造的帧应该是无效的
    EXPECT_FALSE(frame.IsValid());

    // 设置有效值
    frame.width_ = 1920;
    frame.height_ = 1080;
    frame.format_ = PixelFormat::kNV12;
    frame.plane_count_ = 2;
    frame.buffer_size_ = 3110400;
    frame.virtual_address_ = reinterpret_cast<void*>(0x1000);

    // 现在应该是有效的
    EXPECT_TRUE(frame.IsValid());
}

/**
 * @brief 测试 FrameHandle GetPlaneSize 方法
 */
TEST(FrameHandleTest, GetPlaneSize)
{
    FrameHandle frame;
    frame.plane_count_ = 2;
    frame.plane_size_[0] = 2073600; // Y plane
    frame.plane_size_[1] = 1036800; // UV plane

    EXPECT_EQ(frame.GetPlaneSize(0), 2073600);
    EXPECT_EQ(frame.GetPlaneSize(1), 1036800);
    EXPECT_EQ(frame.GetPlaneSize(2), 0); // 超出范围
    EXPECT_EQ(frame.GetPlaneSize(3), 0); // 超出范围
}

/**
 * @brief 测试 FrameHandle GetPlaneData 方法
 */
TEST(FrameHandleTest, GetPlaneData)
{
    FrameHandle frame;
    frame.plane_count_ = 2;
    frame.plane_offset_[0] = 0;
    frame.plane_offset_[1] = 2073600;
    frame.virtual_address_ = reinterpret_cast<void*>(0x10000000);

    void* plane0 = frame.GetPlaneData(0);
    void* plane1 = frame.GetPlaneData(1);
    void* plane2 = frame.GetPlaneData(2);

    EXPECT_EQ(plane0, reinterpret_cast<void*>(0x10000000));
    EXPECT_EQ(plane1, reinterpret_cast<void*>(0x10000000 + 2073600));
    EXPECT_EQ(plane2, nullptr); // 超出范围
}

/**
 * @brief 测试像素格式转换
 */
TEST(FrameHandleTest, PixelFormatToString)
{
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kUnknown), "Unknown");
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kNV12), "NV12");
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kYUYV), "YUYV");
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kRGB888), "RGB888");
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kRGBA8888), "RGBA8888");
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kMJPEG), "MJPEG");
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kH264), "H264");
    EXPECT_STREQ(PixelFormatToString(PixelFormat::kH265), "H265");
}

/**
 * @brief 测试内存类型转换
 */
TEST(FrameHandleTest, MemoryTypeToString)
{
    EXPECT_STREQ(MemoryTypeToString(MemoryType::kMmap), "Mmap");
    EXPECT_STREQ(MemoryTypeToString(MemoryType::kDmaBuf), "DmaBuf");
    EXPECT_STREQ(MemoryTypeToString(MemoryType::kShm), "SharedMemory");
    EXPECT_STREQ(MemoryTypeToString(MemoryType::kHeap), "Heap");
}

/**
 * @brief 测试错误码转换
 */
TEST(FrameHandleTest, GetErrorString)
{
    EXPECT_STREQ(GetErrorString(ErrorCode::kOk), "Success");
    EXPECT_STREQ(GetErrorString(ErrorCode::kErrorInvalidArgument), "Invalid argument");
    EXPECT_STREQ(GetErrorString(ErrorCode::kErrorDeviceNotFound), "Device not found");
    EXPECT_STREQ(GetErrorString(ErrorCode::kErrorIoctlFailed), "Ioctl failed");
    EXPECT_STREQ(GetErrorString(ErrorCode::kErrorMemoryAlloc), "Memory allocation failed");
    EXPECT_STREQ(GetErrorString(ErrorCode::kErrorThreadStartFailed), "Thread start failed");
    EXPECT_STREQ(GetErrorString(ErrorCode::kErrorInvalidState), "Invalid state");
    EXPECT_STREQ(GetErrorString(ErrorCode::kErrorUnknown), "Unknown error");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
