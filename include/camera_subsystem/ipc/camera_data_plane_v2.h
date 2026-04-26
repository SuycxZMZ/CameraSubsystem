/**
 * @file camera_data_plane_v2.h
 * @brief DataPlaneV2 protocol helpers for DMA-BUF fd passing
 */

#ifndef CAMERA_SUBSYSTEM_IPC_CAMERA_DATA_PLANE_V2_H
#define CAMERA_SUBSYSTEM_IPC_CAMERA_DATA_PLANE_V2_H

#include "camera_subsystem/core/frame_descriptor.h"
#include "camera_subsystem/core/types.h"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camera_subsystem
{
namespace ipc
{

constexpr uint32_t kCameraDataV2Magic = 0x43445632; // "CDV2"
constexpr uint32_t kCameraDataV2Version = 2;
constexpr uint32_t kCameraReleaseV2Magic = 0x43525632; // "CRV2"
constexpr uint32_t kCameraReleaseV2Version = 1;
constexpr uint32_t kCameraDataV2MaxPlanes = core::kMaxFramePlanes;
constexpr uint32_t kCameraDataV2MaxFds = core::kMaxFrameFds;
constexpr const char* kDefaultCameraDataV2SocketPath = "/tmp/camera_subsystem_data_v2.sock";
constexpr const char* kDefaultCameraReleaseV2SocketPath = "/tmp/camera_subsystem_release_v2.sock";

enum class CameraDataV2MemoryType : uint32_t
{
    kDmaBuf = 1,
    kMmapCopy = 2,
    kShm = 3
};

enum class CameraReleaseStatus : uint32_t
{
    kOk = 0,
    kDropped = 1,
    kError = 2,
    kTimeout = 3
};

struct CameraDataPlaneDescriptorV2
{
    uint32_t fd_index;
    uint32_t offset;
    uint32_t stride;
    uint32_t length;
    uint32_t bytes_used;
};

struct CameraDataFrameDescriptorV2
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t stream_id;

    uint64_t frame_id;
    uint32_t buffer_id;
    uint32_t consumer_id;

    uint64_t timestamp_ns;
    uint32_t sequence;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t memory_type;
    uint32_t plane_count;
    uint32_t fd_count;
    uint64_t total_bytes_used;
    uint32_t flags;
    uint32_t reserved1;

    CameraDataPlaneDescriptorV2 planes[kCameraDataV2MaxPlanes];
    uint8_t reserved2[64];
};

struct CameraReleaseFrameV2
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t stream_id;

    uint64_t frame_id;
    uint32_t buffer_id;
    uint32_t consumer_id;
    uint32_t status;
    uint32_t reserved0;
    uint64_t timestamp_ns;
    uint8_t reserved1[32];
};

CameraDataFrameDescriptorV2 MakeCameraDataFrameDescriptorV2(
    const core::FrameDescriptor& descriptor);
bool IsCameraDataFrameDescriptorV2Valid(const CameraDataFrameDescriptorV2& descriptor);

CameraReleaseFrameV2 MakeCameraReleaseFrameV2(uint32_t stream_id,
                                             uint64_t frame_id,
                                             uint32_t buffer_id,
                                             uint32_t consumer_id,
                                             CameraReleaseStatus status,
                                             uint64_t timestamp_ns);
bool IsCameraReleaseFrameV2Valid(const CameraReleaseFrameV2& release);

bool SendCameraDataFrameDescriptorV2(int socket_fd,
                                     const CameraDataFrameDescriptorV2& descriptor,
                                     const int* fds,
                                     uint32_t fd_count);
bool ReceiveCameraDataFrameDescriptorV2(int socket_fd,
                                        CameraDataFrameDescriptorV2* descriptor,
                                        int* fds,
                                        uint32_t max_fd_count,
                                        uint32_t* received_fd_count);
bool SendCameraReleaseFrameV2(int socket_fd, const CameraReleaseFrameV2& release);
bool ReceiveCameraReleaseFrameV2(int socket_fd, CameraReleaseFrameV2* release);

struct CameraReleaseReclaim
{
    uint32_t stream_id = 0;
    uint64_t frame_id = 0;
    uint32_t buffer_id = 0;
    CameraReleaseStatus status = CameraReleaseStatus::kOk;
    uint32_t expected_release_count = 0;
    uint32_t observed_release_count = 0;
};

struct CameraReleaseTrackerStats
{
    uint64_t registered_frames = 0;
    uint64_t reclaimed_frames = 0;
    uint64_t timeout_reclaims = 0;
    uint64_t disconnect_reclaims = 0;
    uint64_t duplicate_releases = 0;
    uint64_t unknown_releases = 0;
};

struct CameraReleaseServerStats
{
    uint64_t accepted_clients = 0;
    uint64_t received_releases = 0;
    uint64_t invalid_releases = 0;
    uint64_t reclaimed_frames = 0;
    uint64_t expired_reclaims = 0;
};

class CameraReleaseTracker
{
public:
    explicit CameraReleaseTracker(std::chrono::milliseconds release_timeout =
                                      std::chrono::milliseconds(1000));

    bool RegisterFrame(uint32_t stream_id,
                       uint64_t frame_id,
                       uint32_t buffer_id,
                       const std::vector<uint32_t>& expected_consumers);
    std::vector<CameraReleaseReclaim> MarkReleased(const CameraReleaseFrameV2& release);
    std::vector<CameraReleaseReclaim> ReclaimExpired();
    std::vector<CameraReleaseReclaim> ReclaimConsumerDisconnected(uint32_t consumer_id);

    size_t PendingFrameCount() const;
    CameraReleaseTrackerStats GetStats() const;

private:
    struct FrameKey
    {
        uint32_t stream_id = 0;
        uint64_t frame_id = 0;
        uint32_t buffer_id = 0;

        bool operator==(const FrameKey& other) const;
    };

    struct FrameKeyHash
    {
        size_t operator()(const FrameKey& key) const;
    };

    struct PendingFrame
    {
        FrameKey key;
        std::unordered_set<uint32_t> expected_consumers;
        std::unordered_set<uint32_t> released_consumers;
        std::chrono::steady_clock::time_point deadline;
    };

    CameraReleaseReclaim MakeReclaimLocked(const PendingFrame& frame,
                                           CameraReleaseStatus status) const;

    std::chrono::milliseconds release_timeout_;
    mutable std::mutex mutex_;
    std::unordered_map<FrameKey, PendingFrame, FrameKeyHash> pending_frames_;
    CameraReleaseTrackerStats stats_;
};

class CameraReleaseServer
{
public:
    using ReclaimCallback = std::function<void(const CameraReleaseReclaim&)>;

    explicit CameraReleaseServer(std::chrono::milliseconds release_timeout =
                                     std::chrono::milliseconds(1000));
    ~CameraReleaseServer();

    bool Start(const std::string& socket_path = kDefaultCameraReleaseV2SocketPath,
               ReclaimCallback reclaim_callback = ReclaimCallback());
    void Stop();
    bool IsRunning() const;
    std::string GetSocketPath() const;

    bool RegisterFrame(uint32_t stream_id,
                       uint64_t frame_id,
                       uint32_t buffer_id,
                       const std::vector<uint32_t>& expected_consumers);
    std::vector<CameraReleaseReclaim> ReclaimConsumerDisconnected(uint32_t consumer_id);

    CameraReleaseServerStats GetServerStats() const;
    CameraReleaseTrackerStats GetTrackerStats() const;
    size_t PendingFrameCount() const;

private:
    void AcceptLoop();
    void ClientLoop(int client_fd);
    void ExpireLoop();
    void EmitReclaims(const std::vector<CameraReleaseReclaim>& reclaims);

    CameraReleaseTracker tracker_;
    ReclaimCallback reclaim_callback_;

    int server_fd_ = -1;
    std::string socket_path_;
    std::atomic<bool> is_running_{false};
    std::thread accept_thread_;
    std::thread expire_thread_;

    mutable std::mutex clients_mutex_;
    std::unordered_set<int> client_fds_;
    std::vector<std::thread> client_threads_;

    mutable std::mutex stats_mutex_;
    CameraReleaseServerStats server_stats_;
};

} // namespace ipc
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_IPC_CAMERA_DATA_PLANE_V2_H
