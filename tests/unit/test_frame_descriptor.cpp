#include "camera_subsystem/core/frame_descriptor.h"
#include "camera_subsystem/core/dma_buf_sync.h"
#include "camera_subsystem/core/frame_lease.h"

#include <gtest/gtest.h>

using namespace camera_subsystem::core;

TEST(FrameDescriptorTest, DmaBufDescriptorValidity)
{
    FrameDescriptor descriptor;
    descriptor.frame_id = 42;
    descriptor.width = 1920;
    descriptor.height = 1080;
    descriptor.pixel_format = PixelFormat::kNV12;
    descriptor.memory_type = MemoryType::kDmaBuf;
    descriptor.buffer_id = 1;
    descriptor.plane_count = 1;
    descriptor.fd_count = 1;
    descriptor.fds[0] = 10;
    descriptor.planes[0].fd_index = 0;
    descriptor.planes[0].offset = 0;
    descriptor.planes[0].stride = 1920;
    descriptor.planes[0].length = 1920 * 1080;
    descriptor.planes[0].bytes_used = 1920 * 1080;
    descriptor.total_bytes_used = 1920 * 1080;

    EXPECT_TRUE(descriptor.IsValid());

    descriptor.fds[0] = -1;
    EXPECT_FALSE(descriptor.IsValid());
}

TEST(FrameDescriptorTest, DmaBufFrameLeaseReleaseIsIdempotent)
{
    uint32_t release_count = 0;
    DmaBufFrameLease lease(
        7,
        [&](uint32_t buffer_id)
        {
            EXPECT_EQ(buffer_id, 7u);
            ++release_count;
        });

    EXPECT_FALSE(lease.IsReleased());
    EXPECT_EQ(lease.BufferId(), 7u);

    lease.Release();
    lease.Release();

    EXPECT_TRUE(lease.IsReleased());
    EXPECT_EQ(release_count, 1u);
}

TEST(FrameDescriptorTest, MultiPlaneDmaBufDescriptorValidity)
{
    FrameDescriptor descriptor;
    descriptor.frame_id = 43;
    descriptor.width = 1920;
    descriptor.height = 1080;
    descriptor.pixel_format = PixelFormat::kNV12;
    descriptor.memory_type = MemoryType::kDmaBuf;
    descriptor.buffer_id = 2;
    descriptor.plane_count = 2;
    descriptor.fd_count = 2;
    descriptor.fds[0] = 10;
    descriptor.fds[1] = 11;
    descriptor.planes[0].fd_index = 0;
    descriptor.planes[0].offset = 0;
    descriptor.planes[0].stride = 1920;
    descriptor.planes[0].length = 1920 * 1080;
    descriptor.planes[0].bytes_used = 1920 * 1080;
    descriptor.planes[1].fd_index = 1;
    descriptor.planes[1].offset = 0;
    descriptor.planes[1].stride = 1920;
    descriptor.planes[1].length = 1920 * 540;
    descriptor.planes[1].bytes_used = 1920 * 540;
    descriptor.total_bytes_used = descriptor.planes[0].bytes_used +
                                  descriptor.planes[1].bytes_used;

    EXPECT_TRUE(descriptor.IsValid());

    descriptor.planes[1].fd_index = 2;
    EXPECT_FALSE(descriptor.IsValid());
}

TEST(DmaBufSyncHelperTest, InvalidFdFails)
{
    EXPECT_FALSE(DmaBufSyncHelper::StartCpuAccess(-1, DmaBufSyncDirection::kRead));
    EXPECT_FALSE(DmaBufSyncHelper::EndCpuAccess(-1, DmaBufSyncDirection::kRead));
}

TEST(FrameDescriptorTest, FramePacketRequiresLease)
{
    FramePacket packet;
    packet.descriptor.width = 640;
    packet.descriptor.height = 480;
    packet.descriptor.pixel_format = PixelFormat::kMJPEG;
    packet.descriptor.memory_type = MemoryType::kDmaBuf;
    packet.descriptor.buffer_id = 0;
    packet.descriptor.plane_count = 1;
    packet.descriptor.fd_count = 1;
    packet.descriptor.fds[0] = 11;
    packet.descriptor.planes[0].fd_index = 0;
    packet.descriptor.planes[0].length = 4096;
    packet.descriptor.planes[0].bytes_used = 4096;
    packet.descriptor.total_bytes_used = 4096;

    packet.handle.width_ = 640;
    packet.handle.height_ = 480;
    packet.handle.format_ = PixelFormat::kMJPEG;
    packet.handle.memory_type_ = MemoryType::kDmaBuf;
    packet.handle.buffer_fd_ = 11;
    packet.handle.buffer_size_ = 4096;
    packet.handle.plane_count_ = 1;
    packet.handle.plane_size_[0] = 4096;

    EXPECT_FALSE(packet.IsValid());

    packet.lease = std::make_shared<DmaBufFrameLease>(0, [](uint32_t) {});
    EXPECT_TRUE(packet.IsValid());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
