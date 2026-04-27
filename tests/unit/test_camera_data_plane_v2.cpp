#include "camera_subsystem/ipc/camera_data_plane_v2.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace camera_subsystem::core;
using namespace camera_subsystem::ipc;

namespace
{

FrameDescriptor MakeTestDescriptor(int fd)
{
    FrameDescriptor descriptor;
    descriptor.frame_id = 100;
    descriptor.camera_id = 3;
    descriptor.timestamp_ns = 123456789;
    descriptor.sequence = 77;
    descriptor.width = 640;
    descriptor.height = 480;
    descriptor.pixel_format = PixelFormat::kMJPEG;
    descriptor.memory_type = MemoryType::kDmaBuf;
    descriptor.buffer_id = 2;
    descriptor.plane_count = 1;
    descriptor.fd_count = 1;
    descriptor.fds[0] = fd;
    descriptor.planes[0].fd_index = 0;
    descriptor.planes[0].offset = 0;
    descriptor.planes[0].stride = 0;
    descriptor.planes[0].length = 4096;
    descriptor.planes[0].bytes_used = 1024;
    descriptor.total_bytes_used = 1024;
    return descriptor;
}

size_t CountOpenFds()
{
    DIR* dir = opendir("/proc/self/fd");
    if (dir == nullptr)
    {
        return 0;
    }

    size_t count = 0;
    while (readdir(dir) != nullptr)
    {
        ++count;
    }
    closedir(dir);
    return count;
}

bool SendRawDescriptorWithFd(int socket_fd,
                             const CameraDataFrameDescriptorV2& descriptor,
                             int fd)
{
    struct iovec iov;
    iov.iov_base = const_cast<CameraDataFrameDescriptorV2*>(&descriptor);
    iov.iov_len = sizeof(descriptor);

    alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
    std::memset(control, 0, sizeof(control));

    struct msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    return sendmsg(socket_fd, &msg, MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof(descriptor));
}

} // namespace

int ConnectUnixSocket(const char* path)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}

TEST(CameraDataPlaneV2Test, DescriptorMappingPreservesFrameMetadata)
{
    const FrameDescriptor source = MakeTestDescriptor(10);
    const CameraDataFrameDescriptorV2 descriptor = MakeCameraDataFrameDescriptorV2(source);

    EXPECT_TRUE(IsCameraDataFrameDescriptorV2Valid(descriptor));
    EXPECT_EQ(descriptor.magic, kCameraDataV2Magic);
    EXPECT_EQ(descriptor.version, kCameraDataV2Version);
    EXPECT_EQ(descriptor.stream_id, source.camera_id);
    EXPECT_EQ(descriptor.frame_id, source.frame_id);
    EXPECT_EQ(descriptor.buffer_id, source.buffer_id);
    EXPECT_EQ(descriptor.consumer_id, 0u);
    EXPECT_EQ(descriptor.fd_count, 1u);
    EXPECT_EQ(descriptor.plane_count, 1u);
    EXPECT_EQ(descriptor.planes[0].fd_index, 0u);
    EXPECT_EQ(descriptor.planes[0].bytes_used, 1024u);
}

TEST(CameraDataPlaneV2Test, ReleaseFrameValidation)
{
    const CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(3, 100, 2, 9, CameraReleaseStatus::kOk, 123);

    EXPECT_TRUE(IsCameraReleaseFrameV2Valid(release));
    EXPECT_EQ(release.magic, kCameraReleaseV2Magic);
    EXPECT_EQ(release.stream_id, 3u);
    EXPECT_EQ(release.frame_id, 100u);
    EXPECT_EQ(release.buffer_id, 2u);
    EXPECT_EQ(release.consumer_id, 9u);
    EXPECT_EQ(release.status, static_cast<uint32_t>(CameraReleaseStatus::kOk));
}

TEST(CameraDataPlaneV2Test, SendAndReceiveReleaseFrame)
{
    int sockets[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);

    const CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(3, 100, 2, 9, CameraReleaseStatus::kOk, 123);
    ASSERT_TRUE(SendCameraReleaseFrameV2(sockets[0], release));

    CameraReleaseFrameV2 received;
    ASSERT_TRUE(ReceiveCameraReleaseFrameV2(sockets[1], &received));
    EXPECT_EQ(received.stream_id, release.stream_id);
    EXPECT_EQ(received.frame_id, release.frame_id);
    EXPECT_EQ(received.buffer_id, release.buffer_id);
    EXPECT_EQ(received.consumer_id, release.consumer_id);

    close(sockets[0]);
    close(sockets[1]);
}

TEST(CameraReleaseTrackerTest, ReclaimsAfterAllExpectedConsumersRelease)
{
    CameraReleaseTracker tracker(std::chrono::milliseconds(100));
    ASSERT_TRUE(tracker.RegisterFrame(1, 10, 3, {7, 8}));

    CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(1, 10, 3, 7, CameraReleaseStatus::kOk, 0);
    EXPECT_TRUE(tracker.MarkReleased(release).empty());
    EXPECT_EQ(tracker.PendingFrameCount(), 1u);

    release.consumer_id = 8;
    const auto reclaims = tracker.MarkReleased(release);
    ASSERT_EQ(reclaims.size(), 1u);
    EXPECT_EQ(reclaims[0].status, CameraReleaseStatus::kOk);
    EXPECT_EQ(reclaims[0].expected_release_count, 2u);
    EXPECT_EQ(reclaims[0].observed_release_count, 2u);
    EXPECT_EQ(tracker.PendingFrameCount(), 0u);
}

TEST(CameraReleaseTrackerTest, ReclaimsExpiredFrames)
{
    CameraReleaseTracker tracker(std::chrono::milliseconds(1));
    ASSERT_TRUE(tracker.RegisterFrame(1, 11, 4, {7}));

    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    const auto reclaims = tracker.ReclaimExpired();
    ASSERT_EQ(reclaims.size(), 1u);
    EXPECT_EQ(reclaims[0].status, CameraReleaseStatus::kTimeout);
    EXPECT_EQ(reclaims[0].expected_release_count, 1u);
    EXPECT_EQ(reclaims[0].observed_release_count, 0u);

    const CameraReleaseTrackerStats stats = tracker.GetStats();
    EXPECT_EQ(stats.timeout_reclaims, 1u);
}

TEST(CameraReleaseTrackerTest, ReclaimsOnConsumerDisconnect)
{
    CameraReleaseTracker tracker(std::chrono::milliseconds(100));
    ASSERT_TRUE(tracker.RegisterFrame(1, 12, 5, {7, 8}));

    CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(1, 12, 5, 7, CameraReleaseStatus::kOk, 0);
    EXPECT_TRUE(tracker.MarkReleased(release).empty());

    const auto reclaims = tracker.ReclaimConsumerDisconnected(8);
    ASSERT_EQ(reclaims.size(), 1u);
    EXPECT_EQ(reclaims[0].status, CameraReleaseStatus::kError);
    EXPECT_EQ(reclaims[0].expected_release_count, 2u);
    EXPECT_EQ(reclaims[0].observed_release_count, 2u);

    const CameraReleaseTrackerStats stats = tracker.GetStats();
    EXPECT_EQ(stats.disconnect_reclaims, 1u);
}

TEST(CameraReleaseTrackerTest, TracksDuplicateAndUnknownReleases)
{
    CameraReleaseTracker tracker(std::chrono::milliseconds(100));
    ASSERT_TRUE(tracker.RegisterFrame(1, 13, 5, {7}));

    CameraReleaseFrameV2 unknown =
        MakeCameraReleaseFrameV2(1, 13, 5, 8, CameraReleaseStatus::kOk, 0);
    EXPECT_TRUE(tracker.MarkReleased(unknown).empty());

    CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(1, 13, 5, 7, CameraReleaseStatus::kOk, 0);
    auto reclaims = tracker.MarkReleased(release);
    ASSERT_EQ(reclaims.size(), 1u);

    EXPECT_TRUE(tracker.MarkReleased(release).empty());

    const CameraReleaseTrackerStats stats = tracker.GetStats();
    EXPECT_EQ(stats.unknown_releases, 2u);
    EXPECT_EQ(stats.duplicate_releases, 0u);
}

TEST(CameraReleaseTrackerTest, TracksDuplicateReleaseBeforeReclaim)
{
    CameraReleaseTracker tracker(std::chrono::milliseconds(100));
    ASSERT_TRUE(tracker.RegisterFrame(1, 14, 5, {7, 8}));

    CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(1, 14, 5, 7, CameraReleaseStatus::kOk, 0);
    EXPECT_TRUE(tracker.MarkReleased(release).empty());
    EXPECT_TRUE(tracker.MarkReleased(release).empty());

    const CameraReleaseTrackerStats stats = tracker.GetStats();
    EXPECT_EQ(stats.duplicate_releases, 1u);
    EXPECT_EQ(tracker.PendingFrameCount(), 1u);
}

TEST(CameraReleaseServerTest, ReceivesReleaseAndEmitsReclaim)
{
    const char* socket_path = "/tmp/camera_release_server_test.sock";
    unlink(socket_path);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<CameraReleaseReclaim> reclaims;

    CameraReleaseServer server(std::chrono::milliseconds(100));
    if (!server.Start(
        socket_path,
        [&](const CameraReleaseReclaim& reclaim)
        {
            std::lock_guard<std::mutex> lock(mutex);
            reclaims.push_back(reclaim);
            cv.notify_all();
        }))
    {
        GTEST_SKIP() << "Skip because unix socket bind may be denied by environment: "
                     << socket_path;
    }
    ASSERT_TRUE(server.RegisterFrame(1, 20, 6, {9}));

    const int client_fd = ConnectUnixSocket(socket_path);
    ASSERT_GE(client_fd, 0);

    const CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(1, 20, 6, 9, CameraReleaseStatus::kOk, 0);
    ASSERT_TRUE(SendCameraReleaseFrameV2(client_fd, release));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&]() {
            return !reclaims.empty();
        }));
    }

    EXPECT_EQ(reclaims.size(), 1u);
    EXPECT_EQ(reclaims[0].status, CameraReleaseStatus::kOk);
    EXPECT_EQ(server.PendingFrameCount(), 0u);
    EXPECT_EQ(server.GetServerStats().received_releases, 1u);
    EXPECT_EQ(server.GetServerStats().reclaimed_frames, 1u);

    close(client_fd);
    server.Stop();
    unlink(socket_path);
}

TEST(CameraReleaseServerTest, EmitsTimeoutReclaim)
{
    const char* socket_path = "/tmp/camera_release_server_timeout_test.sock";
    unlink(socket_path);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<CameraReleaseReclaim> reclaims;

    CameraReleaseServer server(std::chrono::milliseconds(10));
    if (!server.Start(
        socket_path,
        [&](const CameraReleaseReclaim& reclaim)
        {
            std::lock_guard<std::mutex> lock(mutex);
            reclaims.push_back(reclaim);
            cv.notify_all();
        }))
    {
        GTEST_SKIP() << "Skip because unix socket bind may be denied by environment: "
                     << socket_path;
    }
    ASSERT_TRUE(server.RegisterFrame(1, 21, 7, {9}));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&]() {
            return !reclaims.empty();
        }));
    }

    EXPECT_EQ(reclaims.size(), 1u);
    EXPECT_EQ(reclaims[0].status, CameraReleaseStatus::kTimeout);
    EXPECT_EQ(server.GetServerStats().expired_reclaims, 1u);

    server.Stop();
    unlink(socket_path);
}

TEST(CameraReleaseServerTest, CountsInvalidReleaseAndContinues)
{
    const char* socket_path = "/tmp/camera_release_server_invalid_test.sock";
    unlink(socket_path);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<CameraReleaseReclaim> reclaims;

    CameraReleaseServer server(std::chrono::milliseconds(100));
    if (!server.Start(
            socket_path,
            [&](const CameraReleaseReclaim& reclaim)
            {
                std::lock_guard<std::mutex> lock(mutex);
                reclaims.push_back(reclaim);
                cv.notify_all();
            }))
    {
        GTEST_SKIP() << "Skip because unix socket bind may be denied by environment: "
                     << socket_path;
    }
    ASSERT_TRUE(server.RegisterFrame(1, 22, 8, {9}));

    const int client_fd = ConnectUnixSocket(socket_path);
    ASSERT_GE(client_fd, 0);

    CameraReleaseFrameV2 invalid =
        MakeCameraReleaseFrameV2(1, 22, 8, 9, CameraReleaseStatus::kOk, 0);
    invalid.magic = 0;
    ASSERT_EQ(write(client_fd, &invalid, sizeof(invalid)), static_cast<ssize_t>(sizeof(invalid)));

    const CameraReleaseFrameV2 valid =
        MakeCameraReleaseFrameV2(1, 22, 8, 9, CameraReleaseStatus::kOk, 0);
    ASSERT_TRUE(SendCameraReleaseFrameV2(client_fd, valid));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&]() {
            return !reclaims.empty();
        }));
    }

    EXPECT_EQ(reclaims.size(), 1u);
    EXPECT_EQ(reclaims[0].status, CameraReleaseStatus::kOk);
    EXPECT_EQ(server.GetServerStats().invalid_releases, 1u);
    EXPECT_EQ(server.GetServerStats().received_releases, 1u);

    close(client_fd);
    server.Stop();
    unlink(socket_path);
}

TEST(CameraReleaseServerTest, EmitsTimeoutAfterPartialRelease)
{
    const char* socket_path = "/tmp/camera_release_server_partial_timeout_test.sock";
    unlink(socket_path);

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<CameraReleaseReclaim> reclaims;

    CameraReleaseServer server(std::chrono::milliseconds(20));
    if (!server.Start(
            socket_path,
            [&](const CameraReleaseReclaim& reclaim)
            {
                std::lock_guard<std::mutex> lock(mutex);
                reclaims.push_back(reclaim);
                cv.notify_all();
            }))
    {
        GTEST_SKIP() << "Skip because unix socket bind may be denied by environment: "
                     << socket_path;
    }
    ASSERT_TRUE(server.RegisterFrame(1, 23, 8, {9, 10}));

    const int client_fd = ConnectUnixSocket(socket_path);
    ASSERT_GE(client_fd, 0);

    const CameraReleaseFrameV2 release =
        MakeCameraReleaseFrameV2(1, 23, 8, 9, CameraReleaseStatus::kOk, 0);
    ASSERT_TRUE(SendCameraReleaseFrameV2(client_fd, release));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [&]() {
            return !reclaims.empty();
        }));
    }

    ASSERT_EQ(reclaims.size(), 1u);
    EXPECT_EQ(reclaims[0].status, CameraReleaseStatus::kTimeout);
    EXPECT_EQ(reclaims[0].expected_release_count, 2u);
    EXPECT_EQ(reclaims[0].observed_release_count, 1u);
    EXPECT_EQ(server.PendingFrameCount(), 0u);

    close(client_fd);
    server.Stop();
    unlink(socket_path);
}

TEST(CameraDataPlaneV2Test, SendAndReceiveDescriptorWithScmRights)
{
    int sockets[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets), 0);

    const int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    ASSERT_GE(fd, 0);

    const FrameDescriptor source = MakeTestDescriptor(fd);
    const CameraDataFrameDescriptorV2 descriptor = MakeCameraDataFrameDescriptorV2(source);
    ASSERT_TRUE(SendCameraDataFrameDescriptorV2(sockets[0], descriptor, &fd, 1));

    CameraDataFrameDescriptorV2 received;
    int received_fds[kCameraDataV2MaxFds] = {-1, -1, -1};
    uint32_t received_fd_count = 0;
    ASSERT_TRUE(ReceiveCameraDataFrameDescriptorV2(
        sockets[1], &received, received_fds, kCameraDataV2MaxFds, &received_fd_count));

    EXPECT_EQ(received_fd_count, 1u);
    EXPECT_GE(received_fds[0], 0);
    EXPECT_NE(received_fds[0], fd);
    EXPECT_EQ(received.frame_id, descriptor.frame_id);
    EXPECT_EQ(received.buffer_id, descriptor.buffer_id);
    EXPECT_EQ(received.planes[0].fd_index, 0u);

    close(received_fds[0]);
    close(fd);
    close(sockets[0]);
    close(sockets[1]);
}

TEST(CameraDataPlaneV2Test, InvalidDescriptorWithScmRightsDoesNotLeakFd)
{
    int sockets[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets), 0);

    const int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    ASSERT_GE(fd, 0);

    CameraDataFrameDescriptorV2 descriptor =
        MakeCameraDataFrameDescriptorV2(MakeTestDescriptor(fd));
    descriptor.magic = 0;

    const size_t before_fd_count = CountOpenFds();
    ASSERT_TRUE(SendRawDescriptorWithFd(sockets[0], descriptor, fd));

    CameraDataFrameDescriptorV2 received;
    int received_fds[kCameraDataV2MaxFds] = {-1, -1, -1};
    uint32_t received_fd_count = 0;
    EXPECT_FALSE(ReceiveCameraDataFrameDescriptorV2(
        sockets[1], &received, received_fds, kCameraDataV2MaxFds, &received_fd_count));
    EXPECT_EQ(received_fd_count, 0u);
    EXPECT_EQ(received_fds[0], -1);

    const size_t after_fd_count = CountOpenFds();
    if (before_fd_count != 0 && after_fd_count != 0)
    {
        EXPECT_EQ(after_fd_count, before_fd_count);
    }

    close(fd);
    close(sockets[0]);
    close(sockets[1]);
}
