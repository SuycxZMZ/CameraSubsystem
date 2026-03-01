/**
 * @file camera_publisher_example.cpp
 * @brief 核心发布端示例程序（独立进程）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 *
 * 用法：
 *   ./camera_publisher_example [device_path] [control_socket] [data_socket]
 *
 * 默认参数：
 * 1. device_path   : CAMERA_SUBSYSTEM_DEFAULT_CAMERA（通常为 /dev/video0）
 * 2. control_socket: /tmp/camera_subsystem_control.sock
 * 3. data_socket   : /tmp/camera_subsystem_data.sock
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
#include "camera_subsystem/ipc/camera_channel_contract.h"
#include "camera_subsystem/ipc/camera_control_server.h"
#include "camera_subsystem/ipc/camera_data_ipc.h"
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
#include <unistd.h>
#include <vector>

namespace
{

using camera_subsystem::camera::CameraSessionManager;
using camera_subsystem::camera::CameraSource;
using camera_subsystem::core::CameraConfig;
using camera_subsystem::core::FrameHandle;
using camera_subsystem::core::LogLevel;
using camera_subsystem::ipc::CameraClientRole;
using camera_subsystem::ipc::CameraControlServer;
using camera_subsystem::ipc::CameraDataFrameHeader;
using camera_subsystem::ipc::CameraEndpoint;
using camera_subsystem::ipc::kCameraDataMagic;
using camera_subsystem::ipc::kCameraDataVersion;
using camera_subsystem::platform::PlatformLogger;

std::atomic<bool> g_running(true);

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

struct PublisherStats
{
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> sent_bytes{0};
    std::atomic<uint64_t> send_fail_count{0};
};

} // namespace

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::string device_path = CAMERA_SUBSYSTEM_DEFAULT_CAMERA;
    std::string control_socket_path = camera_subsystem::ipc::kDefaultCameraControlSocketPath;
    std::string data_socket_path = camera_subsystem::ipc::kDefaultCameraDataSocketPath;

    if (argc > 1)
    {
        device_path = argv[1];
    }
    if (argc > 2)
    {
        control_socket_path = argv[2];
    }
    if (argc > 3)
    {
        data_socket_path = argv[3];
    }

    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "publisher",
                        "publisher start, device=%s, control_socket=%s, data_socket=%s",
                        device_path.c_str(), control_socket_path.c_str(), data_socket_path.c_str());

    DataSocketServer data_server;
    if (!data_server.Start(data_socket_path))
    {
        PlatformLogger::Log(LogLevel::kError, "publisher", "failed to start data server");
        PlatformLogger::Shutdown();
        return 1;
    }

    CameraSource camera_source;
    CameraConfig config = CameraConfig::GetDefault();
    config.fps_ = 30;
    config.buffer_count_ = 4;

    PublisherStats stats;
    std::mutex camera_mutex;

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

    PlatformLogger::Log(LogLevel::kInfo, "publisher",
                        "sec | frames | fps | clients | sent_bytes | send_fail");

    uint64_t elapsed_sec = 0;
    uint64_t last_frames = 0;
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++elapsed_sec;

        const uint64_t frames = stats.frame_count.load();
        const uint64_t fps = frames - last_frames;
        last_frames = frames;

        PlatformLogger::Log(LogLevel::kInfo, "publisher",
                            "sec=%" PRIu64 " | frames=%" PRIu64 " | fps=%" PRIu64
                            " | clients=%zu | sent_bytes=%" PRIu64 " | send_fail=%" PRIu64,
                            elapsed_sec, frames, fps, data_server.GetClientCount(),
                            stats.sent_bytes.load(), stats.send_fail_count.load());
    }

    PlatformLogger::Log(LogLevel::kInfo, "publisher", "publisher stopping...");
    control_server.Stop();
    data_server.Stop();

    {
        std::lock_guard<std::mutex> lock(camera_mutex);
        camera_source.Stop();
    }

    (void)session_manager.UnregisterCorePublisher("camera_publisher_core");
    PlatformLogger::Shutdown();
    return 0;
}
