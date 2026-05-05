/**
 * @file camera_publisher_example.cpp
 * @brief 核心发布端示例程序（独立进程）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 *
 * 用法：
 *   ./camera_publisher_example [device_path] [control_socket] [data_socket] [--io-method mmap|dmabuf]
 *
 * 默认参数：
 * 1. device_path   : CAMERA_SUBSYSTEM_DEFAULT_CAMERA（通常为 /dev/video0）
 * 2. control_socket: /tmp/camera_subsystem_control.sock
 * 3. data_socket   : /tmp/camera_subsystem_data.sock
 * 4. --io-method   : mmap（默认）；dmabuf 启用 DMA-BUF EXPBUF 零拷贝路径
 *
 * 运行流程：
 * 1. 启动控制面服务端（CameraControlServer）与数据面服务端（Unix Socket）。
 * 2. 注册唯一核心发布端（CameraSessionManager::RegisterCorePublisher）。
 * 3. 子发布端/订阅端通过控制面发起 Subscribe 后，触发 CameraSource 启动采集。
 * 4. 每采集到一帧，发布端将帧头+帧数据发送给已连接的数据面客户端。
 * 5. 当订阅引用归零时，触发 CameraSource 停止采集并释放设备。
 * 6. 默认无限运行，收到 Ctrl+C（SIGINT/SIGTERM）后优雅退出。
 *
 * 输出说明：
 * - 每秒打印一次统计信息：sec | frames | fps | clients | sent_bytes | send_fail
 */

#include "camera_subsystem/camera/camera_session_manager.h"
#include "camera_subsystem/camera/camera_source.h"
#include "camera_subsystem/core/frame_lease.h"
#include "camera_subsystem/ipc/camera_channel_contract.h"
#include "camera_subsystem/ipc/camera_control_server.h"
#include "camera_subsystem/ipc/camera_data_ipc.h"
#include "camera_subsystem/ipc/camera_data_plane_v2.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace
{

using camera_subsystem::camera::CameraSessionManager;
using camera_subsystem::camera::CameraSource;
using camera_subsystem::core::CameraConfig;
using camera_subsystem::core::FrameHandle;
using camera_subsystem::core::FrameLease;
using camera_subsystem::core::IoMethod;
using camera_subsystem::core::LogLevel;
using camera_subsystem::ipc::CameraClientRole;
using camera_subsystem::ipc::CameraControlServer;
using camera_subsystem::ipc::CameraDataFrameHeader;
using camera_subsystem::ipc::CameraEndpoint;
using camera_subsystem::ipc::CameraReleaseServer;
using camera_subsystem::ipc::MakeCameraDataFrameDescriptorV2;
using camera_subsystem::ipc::SendCameraDataFrameDescriptorV2;
using camera_subsystem::ipc::kCameraDataMagic;
using camera_subsystem::ipc::kCameraDataVersion;
using camera_subsystem::ipc::kDefaultCameraReleaseV2SocketPath;
using camera_subsystem::platform::PlatformLogger;

std::atomic<bool> g_running(true);

enum class DataPlaneMode
{
    kV1Copy,
    kV2DmaBuf
};

void SignalHandler(int signo)
{
    (void)signo;
    g_running.store(false);
}

bool WriteFull(int fd, const void* buffer, size_t length)
{
    size_t total = 0;
    const auto* data = reinterpret_cast<const uint8_t*>(buffer);

    while (total < length)
    {
        const ssize_t n = write(fd, data + total, length - total);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            return false;
        }
        total += static_cast<size_t>(n);
    }

    return true;
}

class DataSocketServer
{
public:
    DataSocketServer()
        : server_fd_(-1)
        , socket_path_()
        , is_running_(false)
    {
    }

    ~DataSocketServer()
    {
        Stop();
    }

    bool Start(const std::string& socket_path)
    {
        if (is_running_.load())
        {
            return true;
        }

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher",
                                "data socket create failed: %s", strerror(errno));
            return false;
        }

        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        unlink(socket_path.c_str());
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher",
                                "data socket bind failed: path=%s err=%s",
                                socket_path.c_str(), strerror(errno));
            close(fd);
            return false;
        }

        if (listen(fd, 16) < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher",
                                "data socket listen failed: %s", strerror(errno));
            close(fd);
            unlink(socket_path.c_str());
            return false;
        }

        server_fd_ = fd;
        socket_path_ = socket_path;
        is_running_.store(true);
        accept_thread_ = std::thread(&DataSocketServer::AcceptLoop, this);
        return true;
    }

    void Stop()
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
            for (const int fd : clients_)
            {
                shutdown(fd, SHUT_RDWR);
                close(fd);
            }
            clients_.clear();
        }

        if (accept_thread_.joinable())
        {
            accept_thread_.join();
        }

        if (!socket_path_.empty())
        {
            unlink(socket_path_.c_str());
            socket_path_.clear();
        }
    }

    std::vector<int> GetClientsSnapshot() const
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_;
    }

    void RemoveClient(int fd)
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find(clients_.begin(), clients_.end(), fd);
        if (it != clients_.end())
        {
            shutdown(*it, SHUT_RDWR);
            close(*it);
            clients_.erase(it);
        }
    }

    size_t GetClientCount() const
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_.size();
    }

private:
    void AcceptLoop()
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
                PlatformLogger::Log(LogLevel::kWarning, "publisher",
                                    "data socket accept failed: %s", strerror(errno));
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_.push_back(client_fd);
            }

            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "data client connected, total=%zu", GetClientCount());
        }
    }

    int server_fd_;
    std::string socket_path_;
    std::atomic<bool> is_running_;
    std::thread accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<int> clients_;
};

class DataPlaneV2SocketServer
{
public:
    struct Client
    {
        uint32_t consumer_id = 0;
        int fd = -1;
    };

    bool Start(const std::string& socket_path)
    {
        if (is_running_.load())
        {
            return true;
        }

        int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher",
                                "data v2 socket create failed: %s", strerror(errno));
            return false;
        }

        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        unlink(socket_path.c_str());
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher",
                                "data v2 socket bind failed: path=%s err=%s",
                                socket_path.c_str(), strerror(errno));
            close(fd);
            return false;
        }

        if (listen(fd, 16) < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher",
                                "data v2 socket listen failed: %s", strerror(errno));
            close(fd);
            unlink(socket_path.c_str());
            return false;
        }

        server_fd_ = fd;
        socket_path_ = socket_path;
        is_running_.store(true);
        accept_thread_ = std::thread(&DataPlaneV2SocketServer::AcceptLoop, this);
        return true;
    }

    void Stop()
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
            for (const auto& client : clients_)
            {
                shutdown(client.fd, SHUT_RDWR);
                close(client.fd);
            }
            clients_.clear();
        }

        if (accept_thread_.joinable())
        {
            accept_thread_.join();
        }

        if (!socket_path_.empty())
        {
            unlink(socket_path_.c_str());
            socket_path_.clear();
        }
    }

    std::vector<Client> GetClientsSnapshot() const
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_;
    }

    void RemoveClient(uint32_t consumer_id)
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find_if(clients_.begin(), clients_.end(),
                               [consumer_id](const Client& client)
                               {
                                   return client.consumer_id == consumer_id;
                               });
        if (it != clients_.end())
        {
            shutdown(it->fd, SHUT_RDWR);
            close(it->fd);
            clients_.erase(it);
        }
    }

    size_t GetClientCount() const
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_.size();
    }

private:
    void AcceptLoop()
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
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            const uint32_t consumer_id = next_consumer_id_.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_.push_back(Client{consumer_id, client_fd});
            }
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "data v2 client connected, consumer_id=%u total=%zu",
                                consumer_id, GetClientCount());
        }
    }

    int server_fd_ = -1;
    std::string socket_path_;
    std::atomic<bool> is_running_{false};
    std::atomic<uint32_t> next_consumer_id_{1};
    std::thread accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<Client> clients_;
};

struct PublisherStats
{
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> dmabuf_frame_count{0};
    std::atomic<uint64_t> v2_sent_frames{0};
    std::atomic<uint64_t> v2_send_fail_count{0};
    std::atomic<uint64_t> sent_bytes{0};
    std::atomic<uint64_t> send_fail_count{0};
};

} // namespace

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SIG_IGN);

    std::string device_path = CAMERA_SUBSYSTEM_DEFAULT_CAMERA;
    std::string control_socket_path = camera_subsystem::ipc::kDefaultCameraControlSocketPath;
    std::string data_socket_path = camera_subsystem::ipc::kDefaultCameraDataSocketPath;
    std::string release_socket_path = kDefaultCameraReleaseV2SocketPath;
    IoMethod io_method = IoMethod::kMmap;
    DataPlaneMode data_plane_mode = DataPlaneMode::kV1Copy;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--io-method" && i + 1 < argc)
        {
            ++i;
            std::string method = argv[i];
            if (method == "dmabuf")
            {
                io_method = IoMethod::kDmaBuf;
            }
            else if (method == "mmap")
            {
                io_method = IoMethod::kMmap;
            }
            else
            {
                PlatformLogger::Log(LogLevel::kError, "publisher",
                                    "unknown io-method: %s (use mmap or dmabuf)", method.c_str());
                return 1;
            }
        }
        else if (arg == "--release-socket" && i + 1 < argc)
        {
            ++i;
            release_socket_path = argv[i];
        }
        else if (arg == "--data-plane" && i + 1 < argc)
        {
            ++i;
            const std::string mode = argv[i];
            if (mode == "v2")
            {
                data_plane_mode = DataPlaneMode::kV2DmaBuf;
            }
            else if (mode == "v1")
            {
                data_plane_mode = DataPlaneMode::kV1Copy;
            }
            else
            {
                PlatformLogger::Log(LogLevel::kError, "publisher",
                                    "unknown data-plane: %s (use v1 or v2)", mode.c_str());
                return 1;
            }
        }
        else if (arg == "--help" || arg == "-h")
        {
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "usage: %s [device_path] [control_socket] [data_socket] "
                                "[--io-method mmap|dmabuf] [--data-plane v1|v2] "
                                "[--release-socket path]",
                                argv[0]);
            return 0;
        }
        else
        {
            // positional args
            static int pos = 0;
            ++pos;
            if (pos == 1) device_path = arg;
            else if (pos == 2) control_socket_path = arg;
            else if (pos == 3) data_socket_path = arg;
        }
    }

    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "publisher",
                        "publisher start, device=%s, control_socket=%s, data_socket=%s, "
                        "release_socket=%s, io_method=%s, data_plane=%s",
                        device_path.c_str(), control_socket_path.c_str(), data_socket_path.c_str(),
                        release_socket_path.c_str(),
                        io_method == IoMethod::kDmaBuf ? "dmabuf" : "mmap",
                        data_plane_mode == DataPlaneMode::kV2DmaBuf ? "v2" : "v1");

    DataSocketServer data_server;
    DataPlaneV2SocketServer data_v2_server;
    const bool use_data_plane_v2 =
        io_method == IoMethod::kDmaBuf && data_plane_mode == DataPlaneMode::kV2DmaBuf;

    if (!use_data_plane_v2 && !data_server.Start(data_socket_path))
    {
        PlatformLogger::Log(LogLevel::kError, "publisher", "failed to start data server");
        PlatformLogger::Shutdown();
        return 1;
    }
    if (use_data_plane_v2 && !data_v2_server.Start(data_socket_path))
    {
        PlatformLogger::Log(LogLevel::kError, "publisher", "failed to start data v2 server");
        PlatformLogger::Shutdown();
        return 1;
    }

    CameraSource camera_source;
    CameraConfig config = CameraConfig::GetDefault();
    config.fps_ = 30;
    config.buffer_count_ = 4;
    config.io_method_ = static_cast<uint32_t>(io_method);

    PublisherStats stats;
    std::mutex camera_mutex;
    CameraReleaseServer release_server(std::chrono::milliseconds(1000));
    std::mutex lease_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<FrameLease>> pending_leases;
    if (io_method == IoMethod::kDmaBuf)
    {
        if (!release_server.Start(
                release_socket_path,
                [&](const camera_subsystem::ipc::CameraReleaseReclaim& reclaim)
                {
                    std::shared_ptr<FrameLease> lease;
                    {
                        std::lock_guard<std::mutex> lock(lease_mutex);
                        auto it = pending_leases.find(reclaim.frame_id);
                        if (it != pending_leases.end())
                        {
                            lease = std::move(it->second);
                            pending_leases.erase(it);
                        }
                    }
                    if (lease)
                    {
                        lease->Release();
                    }
                    PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                        "release reclaim: stream=%u frame=%" PRIu64
                                        " buffer=%u status=%u observed=%u expected=%u",
                                        reclaim.stream_id,
                                        reclaim.frame_id,
                                        reclaim.buffer_id,
                                        static_cast<uint32_t>(reclaim.status),
                                        reclaim.observed_release_count,
                                        reclaim.expected_release_count);
                }))
        {
            PlatformLogger::Log(LogLevel::kError, "publisher",
                                "failed to start release server: path=%s",
                                release_socket_path.c_str());
            data_server.Stop();
            PlatformLogger::Shutdown();
            return 1;
        }
    }

    camera_source.SetFrameCallbackWithBuffer(
        [&](const FrameHandle& frame,
            const std::shared_ptr<camera_subsystem::core::BufferGuard>& /*buffer_ref*/)
        {
            if (!frame.IsValid() || frame.virtual_address_ == nullptr || frame.buffer_size_ == 0)
            {
                return;
            }

            stats.frame_count.fetch_add(1);

            CameraDataFrameHeader header;
            std::memset(&header, 0, sizeof(header));
            header.magic = kCameraDataMagic;
            header.version = kCameraDataVersion;
            header.width = frame.width_;
            header.height = frame.height_;
            header.pixel_format = static_cast<uint32_t>(frame.format_);
            header.frame_size = static_cast<uint32_t>(frame.buffer_size_);
            header.frame_id = frame.frame_id_;
            header.timestamp_ns = frame.timestamp_ns_;
            header.sequence = frame.sequence_;

            const std::vector<int> clients = data_server.GetClientsSnapshot();
            for (const int fd : clients)
            {
                const bool header_ok = WriteFull(fd, &header, sizeof(header));
                const bool body_ok = header_ok &&
                                     WriteFull(fd, frame.virtual_address_, frame.buffer_size_);
                if (!header_ok || !body_ok)
                {
                    data_server.RemoveClient(fd);
                    stats.send_fail_count.fetch_add(1);
                    continue;
                }
                stats.sent_bytes.fetch_add(frame.buffer_size_);
            }
        });

    // DMA-BUF 模式：注册 FramePacketCallback 接收零拷贝帧
    if (io_method == IoMethod::kDmaBuf)
    {
        camera_source.SetFramePacketCallback(
            [&](const camera_subsystem::core::FramePacket& packet)
            {
                stats.frame_count.fetch_add(1);
                stats.dmabuf_frame_count.fetch_add(1);

                const auto& desc = packet.descriptor;
                if (!use_data_plane_v2)
                {
                    PlatformLogger::Log(LogLevel::kDebug, "publisher",
                                        "dmabuf frame: id=%" PRIu64 " buf=%u fd=%d bytes=%" PRIu64,
                                        desc.frame_id, desc.buffer_id,
                                        desc.fd_count > 0 ? desc.fds[0] : -1,
                                        desc.total_bytes_used);
                    return;
                }

                const std::vector<DataPlaneV2SocketServer::Client> clients =
                    data_v2_server.GetClientsSnapshot();
                if (clients.empty())
                {
                    return;
                }

                std::vector<uint32_t> consumer_ids;
                consumer_ids.reserve(clients.size());
                for (const auto& client : clients)
                {
                    consumer_ids.push_back(client.consumer_id);
                }

                {
                    std::lock_guard<std::mutex> lock(lease_mutex);
                    pending_leases[desc.frame_id] = packet.lease;
                }

                if (!release_server.RegisterFrame(
                        desc.camera_id,
                        desc.frame_id,
                        desc.buffer_id,
                        consumer_ids))
                {
                    std::lock_guard<std::mutex> lock(lease_mutex);
                    pending_leases.erase(desc.frame_id);
                    return;
                }

                auto descriptor_v2 = MakeCameraDataFrameDescriptorV2(desc);
                for (const auto& client : clients)
                {
                    descriptor_v2.consumer_id = client.consumer_id;
                    if (!SendCameraDataFrameDescriptorV2(
                            client.fd,
                            descriptor_v2,
                            desc.fds.data(),
                            desc.fd_count))
                    {
                        data_v2_server.RemoveClient(client.consumer_id);
                        release_server.ReclaimConsumerDisconnected(client.consumer_id);
                        stats.v2_send_fail_count.fetch_add(1);
                        continue;
                    }
                    stats.v2_sent_frames.fetch_add(1);
                }

                PlatformLogger::Log(LogLevel::kDebug, "publisher",
                                    "dmabuf v2 frame: id=%" PRIu64 " buf=%u fd=%d bytes=%" PRIu64,
                                    desc.frame_id, desc.buffer_id,
                                    desc.fd_count > 0 ? desc.fds[0] : -1,
                                    desc.total_bytes_used);
            });
    }

    CameraSessionManager session_manager(
        [&](const CameraEndpoint& endpoint)
        {
            std::lock_guard<std::mutex> lock(camera_mutex);

            camera_source.Stop();
            camera_source.SetDevicePath(endpoint.device_path);

            if (!camera_source.Initialize(config))
            {
                PlatformLogger::Log(LogLevel::kError, "publisher",
                                    "CameraSource initialize failed, device=%s",
                                    endpoint.device_path);
                return false;
            }

            if (!camera_source.Start())
            {
                PlatformLogger::Log(LogLevel::kError, "publisher",
                                    "CameraSource start failed, device=%s",
                                    endpoint.device_path);
                return false;
            }

            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "CameraSource started, device=%s", endpoint.device_path);
            return true;
        },
        [&](const CameraEndpoint& endpoint)
        {
            std::lock_guard<std::mutex> lock(camera_mutex);
            camera_source.Stop();
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "CameraSource stopped, device=%s", endpoint.device_path);
        });

    if (!session_manager.RegisterCorePublisher("camera_publisher_core"))
    {
        PlatformLogger::Log(LogLevel::kError, "publisher", "register core publisher failed");
        data_server.Stop();
        PlatformLogger::Shutdown();
        return 1;
    }

    CameraControlServer control_server(&session_manager);
    if (!control_server.Start(control_socket_path))
    {
        PlatformLogger::Log(LogLevel::kError, "publisher",
                            "failed to start control server: stage=%s errno=%d msg=%s",
                            control_server.GetLastErrorStage().c_str(),
                            control_server.GetLastErrorNo(),
                            control_server.GetLastErrorMessage().c_str());
        data_server.Stop();
        PlatformLogger::Shutdown();
        return 1;
    }

    if (io_method == IoMethod::kDmaBuf)
    {
        PlatformLogger::Log(LogLevel::kInfo, "publisher",
                            "sec | frames | fps | clients | sent_bytes | send_fail | "
                            "dmabuf_enabled | dmabuf_frames | export_fail | lease_exhausted | "
                            "active_leases | lease_max | min_queued | v2_sent | v2_send_fail | "
                            "release_pending | release_received | release_reclaimed | release_timeout");
    }
    else
    {
        PlatformLogger::Log(LogLevel::kInfo, "publisher",
                            "sec | frames | fps | clients | sent_bytes | send_fail");
    }

    uint64_t elapsed_sec = 0;
    uint64_t last_frames = 0;
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++elapsed_sec;

        const uint64_t frames = stats.frame_count.load();
        const uint64_t fps = frames - last_frames;
        last_frames = frames;

        if (io_method == IoMethod::kDmaBuf)
        {
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "sec=%" PRIu64 " | frames=%" PRIu64 " | fps=%" PRIu64
                                " | clients=%zu | sent_bytes=%" PRIu64 " | send_fail=%" PRIu64
                                " | dmabuf_enabled=%u | dmabuf_frames=%" PRIu64
                                " | export_fail=%" PRIu64 " | lease_exhausted=%" PRIu64
                                " | active_leases=%zu | lease_max=%zu | min_queued=%zu"
                                " | v2_sent=%" PRIu64 " | v2_send_fail=%" PRIu64
                                " | release_pending=%zu | release_received=%" PRIu64
                                " | release_reclaimed=%" PRIu64 " | release_timeout=%" PRIu64,
                                elapsed_sec, frames, fps,
                                use_data_plane_v2 ? data_v2_server.GetClientCount()
                                                  : data_server.GetClientCount(),
                                stats.sent_bytes.load(), stats.send_fail_count.load(),
                                camera_source.IsDmaBufPathEnabled() ? 1U : 0U,
                                camera_source.GetDmaBufFrameCount(),
                                camera_source.GetDmaBufExportFailureCount(),
                                camera_source.GetDmaBufLeaseExhaustedCount(),
                                camera_source.GetDmaBufActiveLeaseCount(),
                                camera_source.GetDmaBufLeaseInFlightMax(),
                                camera_source.GetDmaBufMinQueuedCaptureBuffers(),
                                stats.v2_sent_frames.load(),
                                stats.v2_send_fail_count.load(),
                                release_server.PendingFrameCount(),
                                release_server.GetServerStats().received_releases,
                                release_server.GetServerStats().reclaimed_frames,
                                release_server.GetServerStats().expired_reclaims);
        }
        else
        {
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "sec=%" PRIu64 " | frames=%" PRIu64 " | fps=%" PRIu64
                                " | clients=%zu | sent_bytes=%" PRIu64 " | send_fail=%" PRIu64,
                                elapsed_sec, frames, fps, data_server.GetClientCount(),
                                stats.sent_bytes.load(), stats.send_fail_count.load());
        }
    }

    PlatformLogger::Log(LogLevel::kInfo, "publisher", "publisher stopping...");
    control_server.Stop();
    release_server.Stop();
    data_server.Stop();
    data_v2_server.Stop();

    {
        std::lock_guard<std::mutex> lock(lease_mutex);
        pending_leases.clear();
    }

    {
        std::lock_guard<std::mutex> lock(camera_mutex);
        camera_source.Stop();
    }

    (void)session_manager.UnregisterCorePublisher("camera_publisher_core");
    PlatformLogger::Shutdown();
    return 0;
}
