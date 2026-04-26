#include "camera_subsystem/ipc/camera_data_plane_v2.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace camera_subsystem
{
namespace ipc
{

namespace
{

constexpr size_t kUnixSocketPathMaxLength = sizeof(sockaddr_un::sun_path);

uint32_t ToDataV2MemoryType(core::MemoryType memory_type)
{
    switch (memory_type)
    {
        case core::MemoryType::kDmaBuf:
            return static_cast<uint32_t>(CameraDataV2MemoryType::kDmaBuf);
        case core::MemoryType::kShm:
            return static_cast<uint32_t>(CameraDataV2MemoryType::kShm);
        case core::MemoryType::kMmap:
        case core::MemoryType::kHeap:
            return static_cast<uint32_t>(CameraDataV2MemoryType::kMmapCopy);
    }
    return static_cast<uint32_t>(CameraDataV2MemoryType::kMmapCopy);
}

bool IsValidFdList(const int* fds, uint32_t fd_count)
{
    if (fd_count == 0 || fd_count > kCameraDataV2MaxFds || fds == nullptr)
    {
        return false;
    }

    for (uint32_t i = 0; i < fd_count; ++i)
    {
        if (fds[i] < 0)
        {
            return false;
        }
    }
    return true;
}

bool WriteFull(int fd, const void* data, size_t length)
{
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t written = 0;
    while (written < length)
    {
        const ssize_t ret = write(fd, ptr + written, length - written);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (ret == 0)
        {
            return false;
        }
        written += static_cast<size_t>(ret);
    }
    return true;
}

bool ReadFull(int fd, void* data, size_t length)
{
    uint8_t* ptr = static_cast<uint8_t*>(data);
    size_t read_bytes = 0;
    while (read_bytes < length)
    {
        const ssize_t ret = read(fd, ptr + read_bytes, length - read_bytes);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (ret == 0)
        {
            return false;
        }
        read_bytes += static_cast<size_t>(ret);
    }
    return true;
}

} // namespace

CameraDataFrameDescriptorV2 MakeCameraDataFrameDescriptorV2(
    const core::FrameDescriptor& descriptor)
{
    CameraDataFrameDescriptorV2 data;
    std::memset(&data, 0, sizeof(data));

    data.magic = kCameraDataV2Magic;
    data.version = kCameraDataV2Version;
    data.header_size = sizeof(CameraDataFrameDescriptorV2);
    data.stream_id = descriptor.camera_id;
    data.frame_id = descriptor.frame_id;
    data.buffer_id = descriptor.buffer_id;
    data.timestamp_ns = descriptor.timestamp_ns;
    data.sequence = descriptor.sequence;
    data.width = descriptor.width;
    data.height = descriptor.height;
    data.pixel_format = static_cast<uint32_t>(descriptor.pixel_format);
    data.memory_type = ToDataV2MemoryType(descriptor.memory_type);
    data.plane_count = descriptor.plane_count;
    data.fd_count = descriptor.fd_count;
    data.total_bytes_used = descriptor.total_bytes_used;
    data.flags = descriptor.flags;

    const uint32_t plane_count = std::min<uint32_t>(descriptor.plane_count, kCameraDataV2MaxPlanes);
    for (uint32_t i = 0; i < plane_count; ++i)
    {
        data.planes[i].fd_index = descriptor.planes[i].fd_index;
        data.planes[i].offset = descriptor.planes[i].offset;
        data.planes[i].stride = descriptor.planes[i].stride;
        data.planes[i].length = descriptor.planes[i].length;
        data.planes[i].bytes_used = descriptor.planes[i].bytes_used;
    }

    return data;
}

bool IsCameraDataFrameDescriptorV2Valid(const CameraDataFrameDescriptorV2& descriptor)
{
    if (descriptor.magic != kCameraDataV2Magic ||
        descriptor.version != kCameraDataV2Version ||
        descriptor.header_size != sizeof(CameraDataFrameDescriptorV2) ||
        descriptor.width == 0 ||
        descriptor.height == 0 ||
        descriptor.plane_count == 0 ||
        descriptor.plane_count > kCameraDataV2MaxPlanes ||
        descriptor.fd_count == 0 ||
        descriptor.fd_count > kCameraDataV2MaxFds ||
        descriptor.total_bytes_used == 0)
    {
        return false;
    }

    for (uint32_t i = 0; i < descriptor.plane_count; ++i)
    {
        if (descriptor.planes[i].fd_index >= descriptor.fd_count ||
            descriptor.planes[i].length == 0 ||
            descriptor.planes[i].bytes_used > descriptor.planes[i].length)
        {
            return false;
        }
    }
    return true;
}

CameraReleaseFrameV2 MakeCameraReleaseFrameV2(uint32_t stream_id,
                                             uint64_t frame_id,
                                             uint32_t buffer_id,
                                             uint32_t consumer_id,
                                             CameraReleaseStatus status,
                                             uint64_t timestamp_ns)
{
    CameraReleaseFrameV2 release;
    std::memset(&release, 0, sizeof(release));
    release.magic = kCameraReleaseV2Magic;
    release.version = kCameraReleaseV2Version;
    release.header_size = sizeof(CameraReleaseFrameV2);
    release.stream_id = stream_id;
    release.frame_id = frame_id;
    release.buffer_id = buffer_id;
    release.consumer_id = consumer_id;
    release.status = static_cast<uint32_t>(status);
    release.timestamp_ns = timestamp_ns;
    return release;
}

bool IsCameraReleaseFrameV2Valid(const CameraReleaseFrameV2& release)
{
    return release.magic == kCameraReleaseV2Magic &&
           release.version == kCameraReleaseV2Version &&
           release.header_size == sizeof(CameraReleaseFrameV2);
}

bool SendCameraDataFrameDescriptorV2(int socket_fd,
                                     const CameraDataFrameDescriptorV2& descriptor,
                                     const int* fds,
                                     uint32_t fd_count)
{
    if (socket_fd < 0 || !IsCameraDataFrameDescriptorV2Valid(descriptor) ||
        !IsValidFdList(fds, fd_count) || fd_count != descriptor.fd_count)
    {
        return false;
    }

    struct iovec iov;
    iov.iov_base = const_cast<CameraDataFrameDescriptorV2*>(&descriptor);
    iov.iov_len = sizeof(descriptor);

    alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int) * kCameraDataV2MaxFds)];
    std::memset(control, 0, sizeof(control));

    struct msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * fd_count);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fd_count);
    std::memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * fd_count);

    const ssize_t sent = sendmsg(socket_fd, &msg, MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(sizeof(descriptor));
}

bool ReceiveCameraDataFrameDescriptorV2(int socket_fd,
                                        CameraDataFrameDescriptorV2* descriptor,
                                        int* fds,
                                        uint32_t max_fd_count,
                                        uint32_t* received_fd_count)
{
    if (socket_fd < 0 || descriptor == nullptr || fds == nullptr ||
        received_fd_count == nullptr || max_fd_count == 0)
    {
        return false;
    }

    *received_fd_count = 0;
    std::memset(descriptor, 0, sizeof(*descriptor));
    for (uint32_t i = 0; i < max_fd_count; ++i)
    {
        fds[i] = -1;
    }

    struct iovec iov;
    iov.iov_base = descriptor;
    iov.iov_len = sizeof(*descriptor);

    alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int) * kCameraDataV2MaxFds)];
    std::memset(control, 0, sizeof(control));

    struct msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    const ssize_t received = recvmsg(socket_fd, &msg, 0);
    if (received != static_cast<ssize_t>(sizeof(*descriptor)) ||
        !IsCameraDataFrameDescriptorV2Valid(*descriptor))
    {
        return false;
    }

    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
        {
            continue;
        }

        const size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
        const uint32_t fd_count = static_cast<uint32_t>(data_len / sizeof(int));
        const uint32_t copy_count = std::min<uint32_t>(fd_count, max_fd_count);
        std::memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * copy_count);
        *received_fd_count = copy_count;

        const int* received_fds = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (uint32_t i = copy_count; i < fd_count; ++i)
        {
            close(received_fds[i]);
        }
        break;
    }

    return *received_fd_count == descriptor->fd_count;
}

bool SendCameraReleaseFrameV2(int socket_fd, const CameraReleaseFrameV2& release)
{
    if (socket_fd < 0 || !IsCameraReleaseFrameV2Valid(release))
    {
        return false;
    }
    return WriteFull(socket_fd, &release, sizeof(release));
}

bool ReceiveCameraReleaseFrameV2(int socket_fd, CameraReleaseFrameV2* release)
{
    if (socket_fd < 0 || release == nullptr)
    {
        return false;
    }

    std::memset(release, 0, sizeof(*release));
    return ReadFull(socket_fd, release, sizeof(*release)) &&
           IsCameraReleaseFrameV2Valid(*release);
}

CameraReleaseTracker::CameraReleaseTracker(std::chrono::milliseconds release_timeout)
    : release_timeout_(release_timeout)
    , mutex_()
    , pending_frames_()
    , stats_()
{
}

bool CameraReleaseTracker::RegisterFrame(
    uint32_t stream_id,
    uint64_t frame_id,
    uint32_t buffer_id,
    const std::vector<uint32_t>& expected_consumers)
{
    if (expected_consumers.empty())
    {
        return false;
    }

    PendingFrame frame;
    frame.key.stream_id = stream_id;
    frame.key.frame_id = frame_id;
    frame.key.buffer_id = buffer_id;
    frame.deadline = std::chrono::steady_clock::now() + release_timeout_;
    frame.expected_consumers.insert(expected_consumers.begin(), expected_consumers.end());

    if (frame.expected_consumers.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const bool inserted = pending_frames_.emplace(frame.key, std::move(frame)).second;
    if (inserted)
    {
        ++stats_.registered_frames;
    }
    return inserted;
}

std::vector<CameraReleaseReclaim> CameraReleaseTracker::MarkReleased(
    const CameraReleaseFrameV2& release)
{
    std::vector<CameraReleaseReclaim> reclaims;
    if (!IsCameraReleaseFrameV2Valid(release))
    {
        return reclaims;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const FrameKey key{release.stream_id, release.frame_id, release.buffer_id};
    auto it = pending_frames_.find(key);
    if (it == pending_frames_.end())
    {
        ++stats_.unknown_releases;
        return reclaims;
    }

    PendingFrame& frame = it->second;
    if (frame.expected_consumers.find(release.consumer_id) == frame.expected_consumers.end())
    {
        ++stats_.unknown_releases;
        return reclaims;
    }

    if (!frame.released_consumers.insert(release.consumer_id).second)
    {
        ++stats_.duplicate_releases;
        return reclaims;
    }

    if (frame.released_consumers.size() == frame.expected_consumers.size())
    {
        reclaims.push_back(MakeReclaimLocked(frame, CameraReleaseStatus::kOk));
        pending_frames_.erase(it);
        ++stats_.reclaimed_frames;
    }

    return reclaims;
}

std::vector<CameraReleaseReclaim> CameraReleaseTracker::ReclaimExpired()
{
    std::vector<CameraReleaseReclaim> reclaims;
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_frames_.begin(); it != pending_frames_.end();)
    {
        if (it->second.deadline <= now)
        {
            reclaims.push_back(MakeReclaimLocked(it->second, CameraReleaseStatus::kTimeout));
            it = pending_frames_.erase(it);
            ++stats_.reclaimed_frames;
            ++stats_.timeout_reclaims;
        }
        else
        {
            ++it;
        }
    }
    return reclaims;
}

std::vector<CameraReleaseReclaim> CameraReleaseTracker::ReclaimConsumerDisconnected(
    uint32_t consumer_id)
{
    std::vector<CameraReleaseReclaim> reclaims;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_frames_.begin(); it != pending_frames_.end();)
    {
        PendingFrame& frame = it->second;
        if (frame.expected_consumers.find(consumer_id) != frame.expected_consumers.end())
        {
            frame.released_consumers.insert(consumer_id);
        }

        if (frame.released_consumers.size() == frame.expected_consumers.size())
        {
            reclaims.push_back(MakeReclaimLocked(frame, CameraReleaseStatus::kError));
            it = pending_frames_.erase(it);
            ++stats_.reclaimed_frames;
            ++stats_.disconnect_reclaims;
        }
        else
        {
            ++it;
        }
    }

    return reclaims;
}

size_t CameraReleaseTracker::PendingFrameCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_frames_.size();
}

CameraReleaseTrackerStats CameraReleaseTracker::GetStats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool CameraReleaseTracker::FrameKey::operator==(const FrameKey& other) const
{
    return stream_id == other.stream_id &&
           frame_id == other.frame_id &&
           buffer_id == other.buffer_id;
}

size_t CameraReleaseTracker::FrameKeyHash::operator()(const FrameKey& key) const
{
    size_t seed = std::hash<uint32_t>{}(key.stream_id);
    seed ^= std::hash<uint64_t>{}(key.frame_id) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= std::hash<uint32_t>{}(key.buffer_id) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

CameraReleaseReclaim CameraReleaseTracker::MakeReclaimLocked(
    const PendingFrame& frame,
    CameraReleaseStatus status) const
{
    CameraReleaseReclaim reclaim;
    reclaim.stream_id = frame.key.stream_id;
    reclaim.frame_id = frame.key.frame_id;
    reclaim.buffer_id = frame.key.buffer_id;
    reclaim.status = status;
    reclaim.expected_release_count = static_cast<uint32_t>(frame.expected_consumers.size());
    reclaim.observed_release_count = static_cast<uint32_t>(frame.released_consumers.size());
    return reclaim;
}

CameraReleaseServer::CameraReleaseServer(std::chrono::milliseconds release_timeout)
    : tracker_(release_timeout)
{
}

CameraReleaseServer::~CameraReleaseServer()
{
    Stop();
}

bool CameraReleaseServer::Start(const std::string& socket_path,
                                ReclaimCallback reclaim_callback)
{
    if (is_running_.load())
    {
        return true;
    }

    if (socket_path.empty() || socket_path.size() >= kUnixSocketPathMaxLength)
    {
        return false;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    unlink(socket_path.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        close(fd);
        return false;
    }

    if (listen(fd, 16) < 0)
    {
        close(fd);
        unlink(socket_path.c_str());
        return false;
    }

    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
    {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    server_fd_ = fd;
    socket_path_ = socket_path;
    reclaim_callback_ = std::move(reclaim_callback);
    is_running_.store(true);
    accept_thread_ = std::thread(&CameraReleaseServer::AcceptLoop, this);
    expire_thread_ = std::thread(&CameraReleaseServer::ExpireLoop, this);
    return true;
}

void CameraReleaseServer::Stop()
{
    if (!is_running_.load())
    {
        return;
    }

    is_running_.store(false);
    if (server_fd_ >= 0)
    {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const int client_fd : client_fds_)
        {
            shutdown(client_fd, SHUT_RDWR);
            close(client_fd);
        }
        client_fds_.clear();
    }

    if (accept_thread_.joinable())
    {
        accept_thread_.join();
    }
    if (expire_thread_.joinable())
    {
        expire_thread_.join();
    }

    for (auto& thread : client_threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    client_threads_.clear();

    if (!socket_path_.empty())
    {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

bool CameraReleaseServer::IsRunning() const
{
    return is_running_.load();
}

std::string CameraReleaseServer::GetSocketPath() const
{
    return socket_path_;
}

bool CameraReleaseServer::RegisterFrame(
    uint32_t stream_id,
    uint64_t frame_id,
    uint32_t buffer_id,
    const std::vector<uint32_t>& expected_consumers)
{
    return tracker_.RegisterFrame(stream_id, frame_id, buffer_id, expected_consumers);
}

std::vector<CameraReleaseReclaim> CameraReleaseServer::ReclaimConsumerDisconnected(
    uint32_t consumer_id)
{
    auto reclaims = tracker_.ReclaimConsumerDisconnected(consumer_id);
    EmitReclaims(reclaims);
    return reclaims;
}

CameraReleaseServerStats CameraReleaseServer::GetServerStats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return server_stats_;
}

CameraReleaseTrackerStats CameraReleaseServer::GetTrackerStats() const
{
    return tracker_.GetStats();
}

size_t CameraReleaseServer::PendingFrameCount() const
{
    return tracker_.PendingFrameCount();
}

void CameraReleaseServer::AcceptLoop()
{
    while (is_running_.load())
    {
        const int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0)
        {
            if (!is_running_.load())
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.insert(client_fd);
        }
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++server_stats_.accepted_clients;
        }
        client_threads_.emplace_back(&CameraReleaseServer::ClientLoop, this, client_fd);
    }
}

void CameraReleaseServer::ClientLoop(int client_fd)
{
    std::unordered_set<uint32_t> seen_consumers;
    while (is_running_.load())
    {
        CameraReleaseFrameV2 release;
        if (!ReceiveCameraReleaseFrameV2(client_fd, &release))
        {
            break;
        }

        seen_consumers.insert(release.consumer_id);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++server_stats_.received_releases;
        }
        EmitReclaims(tracker_.MarkReleased(release));
    }

    for (const uint32_t consumer_id : seen_consumers)
    {
        EmitReclaims(tracker_.ReclaimConsumerDisconnected(consumer_id));
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_fds_.erase(client_fd);
    }
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
}

void CameraReleaseServer::ExpireLoop()
{
    while (is_running_.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EmitReclaims(tracker_.ReclaimExpired());
    }
}

void CameraReleaseServer::EmitReclaims(const std::vector<CameraReleaseReclaim>& reclaims)
{
    if (reclaims.empty())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        server_stats_.reclaimed_frames += reclaims.size();
        for (const auto& reclaim : reclaims)
        {
            if (reclaim.status == CameraReleaseStatus::kTimeout)
            {
                ++server_stats_.expired_reclaims;
            }
        }
    }

    if (!reclaim_callback_)
    {
        return;
    }

    for (const auto& reclaim : reclaims)
    {
        reclaim_callback_(reclaim);
    }
}

} // namespace ipc
} // namespace camera_subsystem
