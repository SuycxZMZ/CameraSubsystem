/**
 * @file camera_publisher_example.cpp
 * @brief 核心发布端示例程序（独立进程）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 *
 * 用法：
 *   ./camera_publisher_example [device_path] [control_socket] [data_socket] [--io-method
 * mmap|dmabuf]
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
#include "camera_subsystem/utils/video_device_discovery.h"

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
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace
{

using camera_subsystem::camera::CameraSessionManager;
using camera_subsystem::camera::CameraSource;
using camera_subsystem::camera::SourceState;
using camera_subsystem::core::CameraConfig;
using camera_subsystem::core::CameraStreamIdentity;
using camera_subsystem::core::FrameHandle;
using camera_subsystem::core::FrameLease;
using camera_subsystem::core::IoMethod;
using camera_subsystem::core::LogLevel;
using camera_subsystem::core::MetricsAggregator;
using camera_subsystem::core::StreamMetrics;
using camera_subsystem::ipc::CameraClientRole;
using camera_subsystem::ipc::CameraControlServer;
using camera_subsystem::ipc::CameraDataFrameHeader;
using camera_subsystem::ipc::CameraEndpoint;
using camera_subsystem::ipc::CameraReleaseServer;
using camera_subsystem::ipc::CameraReleaseStatus;
using camera_subsystem::ipc::kCameraDataMagic;
using camera_subsystem::ipc::kCameraDataVersion;
using camera_subsystem::ipc::kDefaultCameraReleaseV2SocketPath;
using camera_subsystem::ipc::MakeCameraDataFrameDescriptorV2;
using camera_subsystem::ipc::MakeCameraStreamIdentityFromEndpoint;
using camera_subsystem::ipc::SendCameraDataFrameDescriptorV2;
using camera_subsystem::platform::PlatformLogger;
using camera_subsystem::utils::FindUniqueVideoDeviceByPhysicalId;
using camera_subsystem::utils::FormatVideoDeviceDiscoveryForLog;
using camera_subsystem::utils::InspectVideoDevice;
using camera_subsystem::utils::ScanVideoDevices;
using camera_subsystem::utils::VideoDeviceDiscoveryInfo;

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
    DataSocketServer() : server_fd_(-1), socket_path_(), is_running_(false)
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
            PlatformLogger::Log(LogLevel::kError, "publisher", "data socket create failed: %s",
                                strerror(errno));
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
                                "data socket bind failed: path=%s err=%s", socket_path.c_str(),
                                strerror(errno));
            close(fd);
            return false;
        }

        if (listen(fd, 16) < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher", "data socket listen failed: %s",
                                strerror(errno));
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

            PlatformLogger::Log(LogLevel::kInfo, "publisher", "data client connected, total=%zu",
                                GetClientCount());
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
            PlatformLogger::Log(LogLevel::kError, "publisher", "data v2 socket create failed: %s",
                                strerror(errno));
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
                                "data v2 socket bind failed: path=%s err=%s", socket_path.c_str(),
                                strerror(errno));
            close(fd);
            return false;
        }

        if (listen(fd, 16) < 0)
        {
            PlatformLogger::Log(LogLevel::kError, "publisher", "data v2 socket listen failed: %s",
                                strerror(errno));
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
        auto it = std::find_if(clients_.begin(), clients_.end(), [consumer_id](const Client& client)
                               { return client.consumer_id == consumer_id; });
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
                                "data v2 client connected, consumer_id=%u total=%zu", consumer_id,
                                GetClientCount());
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

class DataPlaneV2MetricsProvider : public camera_subsystem::core::IMetricsProvider
{
  public:
    DataPlaneV2MetricsProvider(const PublisherStats* stats, CameraReleaseServer* release_server)
        : stats_(stats), release_server_(release_server)
    {
    }

    void FillMetrics(StreamMetrics* metrics) const override
    {
        if (!metrics || !stats_ || !release_server_)
        {
            return;
        }
        metrics->v2_sent_frame_count = stats_->v2_sent_frames.load();
        metrics->v2_send_failure_count = stats_->v2_send_fail_count.load();
        metrics->release_pending_count = static_cast<uint64_t>(release_server_->PendingFrameCount());
        const auto rs = release_server_->GetServerStats();
        metrics->release_timeout_count = rs.expired_reclaims;
        metrics->release_reclaimed_count = rs.reclaimed_frames;
    }

  private:
    const PublisherStats* stats_;
    CameraReleaseServer* release_server_;
};

std::string JsonEscapeString(const std::string& raw)
{
    std::string escaped;
    escaped.reserve(raw.size() + 16);
    for (char c : raw)
    {
        switch (c)
        {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    escaped += buf;
                }
                else
                {
                    escaped += c;
                }
                break;
        }
    }
    return escaped;
}

uint64_t NowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string MetricsToJsonLine(const std::vector<StreamMetrics>& streams,
                              const PublisherStats* stats,
                              CameraReleaseServer* release_server)
{
    std::string result;
    result.reserve(1024);

    const uint64_t ts_ns = NowNs();

    result += "{\"timestamp_ns\":" + std::to_string(ts_ns) + ",\"streams\":[";

    for (size_t i = 0; i < streams.size(); ++i)
    {
        const auto& m = streams[i];
        if (i > 0) result += ",";
        result += "{\"stream_id\":\"" + JsonEscapeString(m.stream_id) + "\",";
        result += "\"capture_frame_count\":" + std::to_string(m.capture_frame_count) + ",";
        result += "\"capture_dropped_count\":" + std::to_string(m.capture_dropped_count) + ",";
        result += "\"dma_buf_frame_count\":" + std::to_string(m.dma_buf_frame_count) + ",";
        result += "\"lease_exhausted_count\":" + std::to_string(m.lease_exhausted_count) + ",";
        result += "\"active_lease_count\":" + std::to_string(m.active_lease_count) + ",";
        result += "\"broker_published_count\":" + std::to_string(m.broker_published_count) + ",";
        result += "\"broker_dispatched_count\":" + std::to_string(m.broker_dispatched_count) + ",";
        result += "\"broker_dropped_count\":" + std::to_string(m.broker_dropped_count) + ",";
        result += "\"broker_queue_depth\":" + std::to_string(m.broker_queue_depth) + ",";
        result += "\"broker_subscriber_count\":" + std::to_string(m.broker_subscriber_count) + ",";
        result += "\"v2_sent_frame_count\":" + std::to_string(m.v2_sent_frame_count) + ",";
        result += "\"v2_send_failure_count\":" + std::to_string(m.v2_send_failure_count) + ",";
        result += "\"release_pending_count\":" + std::to_string(m.release_pending_count) + ",";
        result += "\"release_timeout_count\":" + std::to_string(m.release_timeout_count) + ",";
        result += "\"release_reclaimed_count\":" + std::to_string(m.release_reclaimed_count) + ",";
        result += "\"disconnection_count\":" + std::to_string(m.disconnection_count) + ",";
        result += "\"recovery_attempt_count\":" + std::to_string(m.recovery_attempt_count) + ",";
        result += "\"current_state\":" + std::to_string(m.current_state) + ",";
        result += "\"source_degraded\":" + std::string(m.source_degraded ? "true" : "false") + ",";
        result += "\"source_requested_fps\":" + std::to_string(m.source_requested_fps) + ",";
        result += "\"source_current_target_fps\":" + std::to_string(m.source_current_target_fps) + ",";
        result += "\"source_degradation_count\":" + std::to_string(m.source_degradation_count) + ",";
        result += "\"source_degradation_recovery_count\":" + std::to_string(m.source_degradation_recovery_count) + ",";
        result += "\"source_degradation_failure_count\":" + std::to_string(m.source_degradation_failure_count) + "}";
    }

    const uint64_t v2_sent = stats ? stats->v2_sent_frames.load() : 0;
    const uint64_t v2_fail = stats ? stats->v2_send_fail_count.load() : 0;
    const uint64_t rel_pending = release_server ? static_cast<uint64_t>(release_server->PendingFrameCount()) : 0;
    const auto rs = release_server ? release_server->GetServerStats() : camera_subsystem::ipc::CameraReleaseServerStats{};

    result += "],\"global\":{"
              "\"v2_sent_frame_count\":" + std::to_string(v2_sent) + ","
              "\"v2_send_failure_count\":" + std::to_string(v2_fail) + ","
              "\"release_pending_count\":" + std::to_string(rel_pending) + ","
              "\"release_timeout_count\":" + std::to_string(rs.expired_reclaims) + ","
              "\"release_reclaimed_count\":" + std::to_string(rs.reclaimed_frames) + "}}";

    return result;
}

bool AppendMetricsLine(const std::string& path, const std::vector<StreamMetrics>& streams,
                       const PublisherStats* stats, CameraReleaseServer* release_server)
{
    const std::string line = MetricsToJsonLine(streams, stats, release_server);
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        return false;
    }
    const bool ok = WriteFull(fd, line.data(), line.size()) &&
                    WriteFull(fd, "\n", 1);
    close(fd);
    return ok;
}

std::string DiscoveryStateName(const VideoDeviceDiscoveryInfo& info)
{
    if (info.exists)
    {
        return "Bound";
    }
    return "Missing";
}

bool IsKnownPhysicalId(const std::string& physical_id)
{
    return !physical_id.empty() && physical_id != "unknown";
}

struct PendingLeaseKey
{
    uint32_t stream_id = 0;
    uint64_t frame_id = 0;
    uint32_t buffer_id = 0;

    bool operator==(const PendingLeaseKey& other) const
    {
        return stream_id == other.stream_id && frame_id == other.frame_id &&
               buffer_id == other.buffer_id;
    }
};

struct PendingLeaseKeyHash
{
    size_t operator()(const PendingLeaseKey& key) const
    {
        size_t seed = std::hash<uint32_t>{}(key.stream_id);
        seed ^=
            std::hash<uint64_t>{}(key.frame_id) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= std::hash<uint32_t>{}(key.buffer_id) + 0x9e3779b97f4a7c15ULL + (seed << 6) +
                (seed >> 2);
        return seed;
    }
};

struct CameraStreamRuntime
{
    std::mutex mutex;
    CameraSource source;
    CameraStreamIdentity identity;
    std::string configured_device_path;
    std::string current_device_path;
    std::string stable_physical_id;
    std::string discovery_state = "Unknown";
    size_t discovery_candidate_count = 0;
    uint64_t device_rebind_count = 0;
    std::string last_rebind_from;
    std::string last_rebind_to;
    std::string last_rebind_reason;
    std::string last_rebind_error;
    VideoDeviceDiscoveryInfo discovery_info;
    uint64_t discovery_last_refresh_ns = 0;
    std::unordered_map<PendingLeaseKey, std::shared_ptr<FrameLease>, PendingLeaseKeyHash>
        pending_leases;
    bool callbacks_configured = false;
    bool metrics_providers_registered = false;
};

std::string DeviceDiscoveryStatusToJson(
    const std::unordered_map<std::string, std::shared_ptr<CameraStreamRuntime>>& runtimes)
{
    std::string result;
    result.reserve(1024);
    result += "{\"streams\":[";

    bool first = true;
    for (const auto& item : runtimes)
    {
        const auto& runtime = item.second;
        if (!runtime)
        {
            continue;
        }

        std::lock_guard<std::mutex> lock(runtime->mutex);
        const auto& info = runtime->discovery_info;

        if (!first)
        {
            result += ",";
        }
        first = false;

        result += "{\"stream_id\":\"" + JsonEscapeString(runtime->identity.stream_id.data()) +
                  "\",";
        result += "\"camera_id\":" + std::to_string(runtime->identity.camera_id) + ",";
        result += "\"configured_device_path\":\"" +
                  JsonEscapeString(runtime->configured_device_path) + "\",";
        result += "\"current_device_path\":\"" + JsonEscapeString(runtime->current_device_path) +
                  "\",";
        result += "\"discovery_state\":\"" + JsonEscapeString(runtime->discovery_state) + "\",";
        result += "\"device_exists\":" + std::string(info.exists ? "true" : "false") + ",";
        result += "\"can_capture\":" + std::string(info.can_capture ? "true" : "false") + ",";
        result += "\"physical_id\":\"" + JsonEscapeString(info.physical_id) + "\",";
        result += "\"stable_physical_id\":\"" + JsonEscapeString(runtime->stable_physical_id) +
                  "\",";
        result += "\"driver\":\"" + JsonEscapeString(info.driver) + "\",";
        result += "\"name\":\"" + JsonEscapeString(info.name) + "\",";
        result += "\"bus_info\":\"" + JsonEscapeString(info.bus_info) + "\",";
        result += "\"subsystem\":\"" + JsonEscapeString(info.subsystem) + "\",";
        result += "\"vendor_id\":\"" + JsonEscapeString(info.vendor_id) + "\",";
        result += "\"product_id\":\"" + JsonEscapeString(info.product_id) + "\",";
        result += "\"serial\":\"" + JsonEscapeString(info.serial) + "\",";
        result += "\"candidate_count\":" +
                  std::to_string(runtime->discovery_candidate_count) + ",";
        result += "\"rebind_count\":" + std::to_string(runtime->device_rebind_count) + ",";
        result += "\"last_rebind_from\":\"" + JsonEscapeString(runtime->last_rebind_from) +
                  "\",";
        result += "\"last_rebind_to\":\"" + JsonEscapeString(runtime->last_rebind_to) + "\",";
        result += "\"last_rebind_reason\":\"" +
                  JsonEscapeString(runtime->last_rebind_reason) + "\",";
        result += "\"last_rebind_error\":\"" +
                  JsonEscapeString(runtime->last_rebind_error) + "\",";
        result += "\"last_refresh_ns\":" +
                  std::to_string(runtime->discovery_last_refresh_ns) + "}";
    }

    result += "]}";
    return result;
}

bool WriteDeviceDiscoveryStatusFile(
    const std::string& path,
    const std::unordered_map<std::string, std::shared_ptr<CameraStreamRuntime>>& runtimes)
{
    if (path.empty())
    {
        return true;
    }

    const std::string content = DeviceDiscoveryStatusToJson(runtimes);
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        return false;
    }

    const bool ok = WriteFull(fd, content.data(), content.size()) && WriteFull(fd, "\n", 1);
    close(fd);
    return ok;
}

bool TryRebindRuntimeDeviceOnStartFailure(const std::shared_ptr<CameraStreamRuntime>& runtime,
                                          const CameraStreamIdentity& identity,
                                          const CameraConfig& config,
                                          CameraReleaseServer* release_server,
                                          const std::string& failed_device_path,
                                          const std::string& reason)
{
    if (!runtime)
    {
        return false;
    }

    runtime->last_rebind_reason = reason;
    runtime->last_rebind_from = failed_device_path;
    runtime->last_rebind_to.clear();

    std::string physical_id = runtime->stable_physical_id;
    if (!IsKnownPhysicalId(physical_id))
    {
        const auto all_devices = ScanVideoDevices();
        std::vector<VideoDeviceDiscoveryInfo> usb_capture_candidates;
        for (const auto& d : all_devices)
        {
            if (d.exists && d.can_capture && d.subsystem == "usb" &&
                IsKnownPhysicalId(d.physical_id) && d.device_path != failed_device_path)
            {
                usb_capture_candidates.push_back(d);
            }
        }
        if (usb_capture_candidates.size() == 1)
        {
            physical_id = usb_capture_candidates[0].physical_id;
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "stable_physical_id inferred from single USB capture candidate: stream=%s physical_id=%s",
                                identity.stream_id.data(), physical_id.c_str());
        }
        else
        {
            runtime->discovery_state = "Missing";
            runtime->last_rebind_error = usb_capture_candidates.empty()
                ? "stable_physical_id_unavailable"
                : "stable_physical_id_unavailable_ambiguous_usb";
            return false;
        }
    }

    if (!runtime->pending_leases.empty() ||
        (release_server && release_server->PendingFrameCount() > 0))
    {
        runtime->discovery_state = "RebindBlocked";
        runtime->last_rebind_error = "pending_leases_or_releases";
        return false;
    }

    const auto devices = ScanVideoDevices();
    const auto match = FindUniqueVideoDeviceByPhysicalId(devices, physical_id);
    runtime->discovery_candidate_count = match.candidate_count;

    if (!match.has_unique_match)
    {
        runtime->discovery_state = match.candidate_count == 0 ? "Missing" : "Ambiguous";
        runtime->last_rebind_error =
            match.candidate_count == 0 ? "candidate_missing" : "candidate_ambiguous";
        return false;
    }

    const std::string candidate_path = match.device.device_path;
    if (candidate_path.empty() || candidate_path == failed_device_path)
    {
        runtime->discovery_state = "Missing";
        runtime->last_rebind_error = "candidate_not_changed";
        return false;
    }

    PlatformLogger::Log(LogLevel::kInfo, "publisher",
                        "trying device rebind: stream=%s physical_id=%s from=%s to=%s reason=%s",
                        identity.stream_id.data(), physical_id.c_str(), failed_device_path.c_str(),
                        candidate_path.c_str(), reason.c_str());

    runtime->source.Stop();
    runtime->source.SetStreamIdentity(identity);
    runtime->source.SetDevicePath(candidate_path);

    if (!runtime->source.Initialize(config))
    {
        runtime->source.Stop();
        runtime->source.SetDevicePath(failed_device_path);
        runtime->discovery_state = "CapabilityChanged";
        runtime->last_rebind_error = "candidate_initialize_failed";
        PlatformLogger::Log(LogLevel::kWarning, "publisher",
                            "device rebind initialize failed: stream=%s from=%s to=%s",
                            identity.stream_id.data(), failed_device_path.c_str(),
                            candidate_path.c_str());
        return false;
    }

    if (!runtime->source.Start())
    {
        runtime->source.Stop();
        runtime->source.SetDevicePath(failed_device_path);
        runtime->discovery_state = "CapabilityChanged";
        runtime->last_rebind_error = "candidate_start_failed";
        PlatformLogger::Log(LogLevel::kWarning, "publisher",
                            "device rebind start failed: stream=%s from=%s to=%s",
                            identity.stream_id.data(), failed_device_path.c_str(),
                            candidate_path.c_str());
        return false;
    }

    runtime->current_device_path = candidate_path;
    runtime->discovery_info = match.device;
    runtime->discovery_state = "Bound";
    runtime->discovery_last_refresh_ns = NowNs();
    runtime->last_rebind_to = candidate_path;
    runtime->last_rebind_error.clear();
    ++runtime->device_rebind_count;

    PlatformLogger::Log(LogLevel::kInfo, "publisher",
                        "device rebind succeeded: stream=%s physical_id=%s from=%s to=%s",
                        identity.stream_id.data(), physical_id.c_str(), failed_device_path.c_str(),
                        candidate_path.c_str());
    return true;
}

const char* SourceStateName(SourceState state)
{
    switch (state)
    {
        case SourceState::kIdle:
            return "idle";
        case SourceState::kReady:
            return "ready";
        case SourceState::kStreaming:
            return "streaming";
        case SourceState::kDisconnected:
            return "disconnected";
        case SourceState::kRetrying:
            return "retrying";
        case SourceState::kPermanentFailure:
            return "permanent_failure";
        default:
            return "unknown";
    }
}

struct SourceStatusSnapshot
{
    SourceState state = SourceState::kIdle;
    uint64_t disconnections = 0;
    uint64_t recovery_attempts = 0;
};

SourceStatusSnapshot GetAggregateSourceStatus(
    const std::unordered_map<std::string, std::shared_ptr<CameraStreamRuntime>>& runtimes)
{
    SourceStatusSnapshot snapshot;
    bool has_runtime = false;
    for (const auto& item : runtimes)
    {
        const auto& runtime = item.second;
        if (!runtime)
        {
            continue;
        }

        const SourceState state = runtime->source.GetState();
        if (!has_runtime || state > snapshot.state)
        {
            snapshot.state = state;
        }
        snapshot.disconnections += runtime->source.GetDisconnectionCount();
        snapshot.recovery_attempts += runtime->source.GetRecoveryAttemptCount();
        has_runtime = true;
    }
    return snapshot;
}

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
    bool enable_auto_recovery = false;
    bool enable_degradation = false;
    uint32_t degradation_target_fps = 15;
    uint32_t degradation_window_sec = 5;
    uint32_t degradation_recovery_frames = 30;
    uint32_t disconnect_threshold = 3;
    uint32_t max_recovery_attempts = 10;
    uint32_t recovery_backoff_base_ms = 1000;
    uint32_t recovery_backoff_max_ms = 30000;
    std::string metrics_history_path;
    std::string metrics_snapshot_path;
    std::string device_discovery_status_path;
    uint32_t metrics_history_interval_sec = 5;
    bool enable_device_rebind_on_start_failure = false;
    bool enable_device_rebind_on_recovery_failure = false;

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
        else if (arg == "--enable-auto-recovery")
        {
            enable_auto_recovery = true;
        }
        else if (arg == "--disconnect-threshold" && i + 1 < argc)
        {
            ++i;
            disconnect_threshold = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (arg == "--max-recovery-attempts" && i + 1 < argc)
        {
            ++i;
            max_recovery_attempts = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (arg == "--recovery-backoff-ms" && i + 1 < argc)
        {
            ++i;
            recovery_backoff_base_ms = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (arg == "--recovery-backoff-max-ms" && i + 1 < argc)
        {
            ++i;
            recovery_backoff_max_ms = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (arg == "--enable-degradation")
        {
            enable_degradation = true;
        }
        else if (arg == "--degradation-target-fps" && i + 1 < argc)
        {
            ++i;
            degradation_target_fps = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (arg == "--degradation-window-sec" && i + 1 < argc)
        {
            ++i;
            degradation_window_sec = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (arg == "--degradation-recovery-frames" && i + 1 < argc)
        {
            ++i;
            degradation_recovery_frames = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
        }
        else if (arg == "--metrics-history-path" && i + 1 < argc)
        {
            ++i;
            metrics_history_path = argv[i];
        }
        else if (arg == "--metrics-snapshot-path" && i + 1 < argc)
        {
            ++i;
            metrics_snapshot_path = argv[i];
        }
        else if (arg == "--metrics-history-interval" && i + 1 < argc)
        {
            ++i;
            metrics_history_interval_sec = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
            if (metrics_history_interval_sec == 0)
            {
                metrics_history_interval_sec = 5;
            }
        }
        else if (arg == "--device-discovery-status-path" && i + 1 < argc)
        {
            ++i;
            device_discovery_status_path = argv[i];
        }
        else if (arg == "--enable-device-rebind-on-start-failure")
        {
            enable_device_rebind_on_start_failure = true;
        }
        else if (arg == "--enable-device-rebind-on-recovery-failure")
        {
            enable_device_rebind_on_recovery_failure = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "usage: %s [device_path] [control_socket] [data_socket] "
                                "[--io-method mmap|dmabuf] [--data-plane v1|v2] "
                                "[--release-socket path] [--enable-auto-recovery] "
                                "[--enable-degradation] [--degradation-target-fps n] "
                                "[--degradation-window-sec n] [--degradation-recovery-frames n] "
                                "[--disconnect-threshold n] [--max-recovery-attempts n] "
                                "[--recovery-backoff-ms ms] [--recovery-backoff-max-ms ms] "
                                "[--metrics-history-path path] [--metrics-snapshot-path path] "
                                "[--metrics-history-interval sec] "
                                "[--device-discovery-status-path path] "
                                "[--enable-device-rebind-on-start-failure]",
                                argv[0]);
            return 0;
        }
        else
        {
            // positional args
            static int pos = 0;
            ++pos;
            if (pos == 1)
                device_path = arg;
            else if (pos == 2)
                control_socket_path = arg;
            else if (pos == 3)
                data_socket_path = arg;
        }
    }

    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "publisher",
                        "publisher start, device=%s, control_socket=%s, data_socket=%s, "
                        "release_socket=%s, io_method=%s, data_plane=%s, auto_recovery=%s, "
                        "degradation=%s, device_discovery_status=%s, start_failure_rebind=%s, recovery_failure_rebind=%s",
                        device_path.c_str(), control_socket_path.c_str(), data_socket_path.c_str(),
                        release_socket_path.c_str(),
                        io_method == IoMethod::kDmaBuf ? "dmabuf" : "mmap",
                        data_plane_mode == DataPlaneMode::kV2DmaBuf ? "v2" : "v1",
                        enable_auto_recovery ? "enabled" : "disabled",
                        enable_degradation ? "enabled" : "disabled",
                        device_discovery_status_path.empty() ? "disabled" : "enabled",
                        enable_device_rebind_on_start_failure ? "enabled" : "disabled",
                        enable_device_rebind_on_recovery_failure ? "enabled" : "disabled");

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

    CameraConfig config = CameraConfig::GetDefault();
    config.fps_ = 30;
    config.buffer_count_ = 4;
    config.io_method_ = static_cast<uint32_t>(io_method);
    config.enable_auto_recovery = enable_auto_recovery;
    config.enable_degradation = enable_degradation ? 1u : 0u;
    config.degradation_target_fps = degradation_target_fps;
    config.degradation_window_sec = degradation_window_sec;
    config.degradation_recovery_frames = degradation_recovery_frames;
    config.disconnect_threshold = std::max<uint32_t>(1, disconnect_threshold);
    config.max_recovery_attempts = std::max<uint32_t>(1, max_recovery_attempts);
    config.recovery_backoff_base_ms = std::max<uint32_t>(1, recovery_backoff_base_ms);
    config.recovery_backoff_max_ms =
        std::max(config.recovery_backoff_base_ms, recovery_backoff_max_ms);

    PublisherStats stats;
    MetricsAggregator metrics_aggregator;
    std::mutex runtimes_mutex;
    std::unordered_map<std::string, std::shared_ptr<CameraStreamRuntime>> runtimes_by_stream;
    std::unordered_map<uint32_t, std::weak_ptr<CameraStreamRuntime>> runtimes_by_camera_id;
    CameraReleaseServer release_server(std::chrono::milliseconds(1000));
    DataPlaneV2MetricsProvider dp_metrics_provider(&stats, &release_server);
    if (io_method == IoMethod::kDmaBuf)
    {
        if (!release_server.Start(
                release_socket_path,
                [&](const camera_subsystem::ipc::CameraReleaseReclaim& reclaim)
                {
                    std::shared_ptr<FrameLease> lease;
                    std::shared_ptr<CameraStreamRuntime> runtime;
                    {
                        std::lock_guard<std::mutex> lock(runtimes_mutex);
                        auto it = runtimes_by_camera_id.find(reclaim.stream_id);
                        if (it != runtimes_by_camera_id.end())
                        {
                            runtime = it->second.lock();
                        }
                    }
                    if (runtime)
                    {
                        std::lock_guard<std::mutex> lock(runtime->mutex);
                        const PendingLeaseKey key{reclaim.stream_id, reclaim.frame_id,
                                                  reclaim.buffer_id};
                        auto it = runtime->pending_leases.find(key);
                        if (it != runtime->pending_leases.end())
                        {
                            lease = std::move(it->second);
                            runtime->pending_leases.erase(it);
                        }
                    }
                    if (lease)
                    {
                        lease->Release();
                    }
                    if (reclaim.status == CameraReleaseStatus::kError &&
                        reclaim.disconnected_consumer_id != 0)
                    {
                        data_v2_server.RemoveClient(reclaim.disconnected_consumer_id);
                    }
                    PlatformLogger::Log(
                        LogLevel::kInfo, "publisher",
                        "release reclaim: stream=%u frame=%" PRIu64
                        " buffer=%u status=%u observed=%u expected=%u"
                        " disconnected_consumer=%u",
                        reclaim.stream_id, reclaim.frame_id, reclaim.buffer_id,
                        static_cast<uint32_t>(reclaim.status), reclaim.observed_release_count,
                        reclaim.expected_release_count, reclaim.disconnected_consumer_id);
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

    auto configure_runtime_callbacks = [&](const std::shared_ptr<CameraStreamRuntime>& runtime)
    {
        if (!runtime || runtime->callbacks_configured)
        {
            return;
        }

        std::weak_ptr<CameraStreamRuntime> weak_runtime = runtime;
        runtime->source.SetFrameCallbackWithBuffer(
            [&](const FrameHandle& frame,
                const std::shared_ptr<camera_subsystem::core::BufferGuard>& /*buffer_ref*/)
            {
                if (!frame.IsValid() || frame.virtual_address_ == nullptr ||
                    frame.buffer_size_ == 0)
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
                    const bool body_ok =
                        header_ok && WriteFull(fd, frame.virtual_address_, frame.buffer_size_);
                    if (!header_ok || !body_ok)
                    {
                        data_server.RemoveClient(fd);
                        stats.send_fail_count.fetch_add(1);
                        continue;
                    }
                    stats.sent_bytes.fetch_add(frame.buffer_size_);
                }
            });

        if (io_method == IoMethod::kDmaBuf)
        {
            runtime->source.SetFramePacketCallback(
                [&, weak_runtime](const camera_subsystem::core::FramePacket& packet)
                {
                    stats.frame_count.fetch_add(1);
                    stats.dmabuf_frame_count.fetch_add(1);

                    const auto& desc = packet.descriptor;
                    if (!use_data_plane_v2)
                    {
                        PlatformLogger::Log(
                            LogLevel::kDebug, "publisher",
                            "dmabuf frame: stream=%s id=%" PRIu64 " buf=%u fd=%d bytes=%" PRIu64,
                            desc.stream_id.data(), desc.frame_id, desc.buffer_id,
                            desc.fd_count > 0 ? desc.fds[0] : -1, desc.total_bytes_used);
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

                    auto runtime = weak_runtime.lock();
                    if (!runtime)
                    {
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(runtime->mutex);
                        const PendingLeaseKey key{desc.camera_id, desc.frame_id, desc.buffer_id};
                        runtime->pending_leases[key] = packet.lease;
                    }

                    if (!release_server.RegisterFrame(desc.camera_id, desc.frame_id, desc.buffer_id,
                                                      consumer_ids))
                    {
                        std::lock_guard<std::mutex> lock(runtime->mutex);
                        const PendingLeaseKey key{desc.camera_id, desc.frame_id, desc.buffer_id};
                        runtime->pending_leases.erase(key);
                        return;
                    }

                    auto descriptor_v2 = MakeCameraDataFrameDescriptorV2(desc);
                    for (const auto& client : clients)
                    {
                        descriptor_v2.consumer_id = client.consumer_id;
                        if (!SendCameraDataFrameDescriptorV2(client.fd, descriptor_v2,
                                                             desc.fds.data(), desc.fd_count))
                        {
                            data_v2_server.RemoveClient(client.consumer_id);
                            release_server.ReclaimConsumerDisconnected(client.consumer_id);
                            stats.v2_send_fail_count.fetch_add(1);
                            continue;
                        }
                        stats.v2_sent_frames.fetch_add(1);
                    }

                    PlatformLogger::Log(
                        LogLevel::kDebug, "publisher",
                        "dmabuf v2 frame: stream=%s id=%" PRIu64 " buf=%u fd=%d bytes=%" PRIu64,
                        desc.stream_id.data(), desc.frame_id, desc.buffer_id,
                        desc.fd_count > 0 ? desc.fds[0] : -1, desc.total_bytes_used);
                });
        }

        runtime->callbacks_configured = true;
    };

    CameraSessionManager session_manager(
        [&](const CameraEndpoint& endpoint)
        {
            const CameraStreamIdentity identity = MakeCameraStreamIdentityFromEndpoint(endpoint);
            const std::string stream_id = identity.stream_id.data();

            std::shared_ptr<CameraStreamRuntime> runtime;
            {
                std::lock_guard<std::mutex> lock(runtimes_mutex);
                auto it = runtimes_by_stream.find(stream_id);
                if (it == runtimes_by_stream.end())
                {
                    runtime = std::make_shared<CameraStreamRuntime>();
                    runtime->identity = identity;
                    configure_runtime_callbacks(runtime);
                    runtimes_by_stream.emplace(stream_id, runtime);
                    runtimes_by_camera_id[identity.camera_id] = runtime;
                }
                else
                {
                    runtime = it->second;
                }
            }

            std::lock_guard<std::mutex> lock(runtime->mutex);
            if (runtime->source.IsRunning())
            {
                PlatformLogger::Log(
                    LogLevel::kInfo, "publisher",
                    "CameraSource already running, stream=%s camera_id=%u device=%s",
                    identity.stream_id.data(), identity.camera_id, endpoint.device_path);
                return true;
            }

            runtime->source.SetStreamIdentity(identity);
            runtime->source.SetDevicePath(endpoint.device_path);

            const auto discovery_info = InspectVideoDevice(endpoint.device_path);
            runtime->configured_device_path = endpoint.device_path;
            runtime->current_device_path = endpoint.device_path;
            runtime->discovery_info = discovery_info;
            runtime->discovery_state = DiscoveryStateName(discovery_info);
            runtime->discovery_last_refresh_ns = NowNs();
            runtime->discovery_candidate_count = 0;
            if (IsKnownPhysicalId(discovery_info.physical_id))
            {
                runtime->stable_physical_id = discovery_info.physical_id;
            }
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "device discovery snapshot: stream=%s %s",
                                identity.stream_id.data(),
                                FormatVideoDeviceDiscoveryForLog(discovery_info).c_str());

            if (!runtime->source.Initialize(config))
            {
                PlatformLogger::Log(LogLevel::kError, "publisher",
                                    "CameraSource initialize failed, stream=%s device=%s",
                                    identity.stream_id.data(), endpoint.device_path);
                if (!enable_device_rebind_on_start_failure ||
                    !TryRebindRuntimeDeviceOnStartFailure(runtime,
                                                          identity,
                                                          config,
                                                          &release_server,
                                                          endpoint.device_path,
                                                          "initialize_failed"))
                {
                    return false;
                }
            }
            else if (!runtime->source.Start())
            {
                PlatformLogger::Log(LogLevel::kError, "publisher",
                                    "CameraSource start failed, stream=%s device=%s",
                                    identity.stream_id.data(), endpoint.device_path);
                if (!enable_device_rebind_on_start_failure ||
                    !TryRebindRuntimeDeviceOnStartFailure(runtime,
                                                          identity,
                                                          config,
                                                          &release_server,
                                                          endpoint.device_path,
                                                          "start_failed"))
                {
                    return false;
                }
            }

            if (!runtime->metrics_providers_registered)
            {
                metrics_aggregator.RegisterProvider(stream_id, &runtime->source);
                metrics_aggregator.RegisterProvider(stream_id, &dp_metrics_provider);
                runtime->metrics_providers_registered = true;
            }

            if (enable_device_rebind_on_recovery_failure)
            {
                runtime->source.SetRecoveryFailedHook(
                    [runtime, identity, config, &release_server]
                    (const std::string& failed_device_path)
                    {
                        std::thread([runtime, identity, config, &release_server, failed_device_path]()
                                    {
                                        std::lock_guard<std::mutex> lock(runtime->mutex);
                                        TryRebindRuntimeDeviceOnStartFailure(
                                            runtime,
                                            identity,
                                            config,
                                            &release_server,
                                            failed_device_path,
                                            "recovery_failed");
                                    })
                            .detach();
                    });
            }

            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "CameraSource started, stream=%s camera_id=%u device=%s",
                                identity.stream_id.data(), identity.camera_id,
                                runtime->current_device_path.c_str());
            return true;
        },
        [&](const CameraEndpoint& endpoint)
        {
            const CameraStreamIdentity identity = MakeCameraStreamIdentityFromEndpoint(endpoint);
            std::shared_ptr<CameraStreamRuntime> runtime;
            {
                std::lock_guard<std::mutex> lock(runtimes_mutex);
                auto it = runtimes_by_stream.find(identity.stream_id.data());
                if (it != runtimes_by_stream.end())
                {
                    runtime = it->second;
                }
            }
            if (runtime)
            {
                if (runtime->metrics_providers_registered)
                {
                    metrics_aggregator.UnregisterProvider(identity.stream_id.data(), &runtime->source);
                    metrics_aggregator.UnregisterProvider(identity.stream_id.data(), &dp_metrics_provider);
                    runtime->metrics_providers_registered = false;
                }
                runtime->source.Stop();
            }
            PlatformLogger::Log(LogLevel::kInfo, "publisher",
                                "CameraSource stopped, stream=%s device=%s",
                                identity.stream_id.data(), endpoint.device_path);
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
        PlatformLogger::Log(
            LogLevel::kInfo, "publisher",
            "sec | frames | fps | clients | sent_bytes | send_fail | "
            "dmabuf_enabled | dmabuf_frames | export_fail | lease_exhausted | "
            "active_leases | lease_max | min_queued | v2_sent | v2_send_fail | "
            "release_pending | release_received | release_reclaimed | release_timeout | "
            "source_state | disconnects | recovery_attempts");
    }
    else
    {
        PlatformLogger::Log(LogLevel::kInfo, "publisher",
                            "sec | frames | fps | clients | sent_bytes | send_fail | "
                            "source_state | disconnects | recovery_attempts");
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
            bool dmabuf_enabled = false;
            uint64_t dmabuf_frames = 0;
            uint64_t export_failures = 0;
            uint64_t lease_exhausted = 0;
            size_t active_leases = 0;
            size_t lease_in_flight_max = 0;
            size_t min_queued = 0;
            SourceStatusSnapshot source_status;

            const auto all_metrics = metrics_aggregator.GetAllStreamMetrics();
            for (const auto& m : all_metrics)
            {
                dmabuf_enabled = dmabuf_enabled || (m.dma_buf_frame_count > 0);
                dmabuf_frames += m.dma_buf_frame_count;
                lease_exhausted += m.lease_exhausted_count;
                active_leases += m.active_lease_count;
            }

            // 补充全局非 per-stream 指标
            {
                std::lock_guard<std::mutex> lock(runtimes_mutex);
                for (const auto& item : runtimes_by_stream)
                {
                    const auto& runtime = item.second;
                    if (!runtime)
                    {
                        continue;
                    }
                    source_status = GetAggregateSourceStatus(runtimes_by_stream);
                    dmabuf_enabled = dmabuf_enabled || runtime->source.IsDmaBufPathEnabled();
                    export_failures += runtime->source.GetDmaBufExportFailureCount();
                    lease_in_flight_max =
                        std::max(lease_in_flight_max, runtime->source.GetDmaBufLeaseInFlightMax());
                    min_queued += runtime->source.GetDmaBufMinQueuedCaptureBuffers();
                }
            }

            PlatformLogger::Log(
                LogLevel::kInfo, "publisher",
                "sec=%" PRIu64 " | frames=%" PRIu64 " | fps=%" PRIu64
                " | clients=%zu | sent_bytes=%" PRIu64 " | send_fail=%" PRIu64
                " | dmabuf_enabled=%u | dmabuf_frames=%" PRIu64 " | export_fail=%" PRIu64
                " | lease_exhausted=%" PRIu64
                " | active_leases=%zu | lease_max=%zu | min_queued=%zu"
                " | v2_sent=%" PRIu64 " | v2_send_fail=%" PRIu64
                " | release_pending=%zu | release_received=%" PRIu64 " | release_reclaimed=%" PRIu64
                " | release_timeout=%" PRIu64 " | source_state=%s | disconnects=%" PRIu64
                " | recovery_attempts=%" PRIu64,
                elapsed_sec, frames, fps,
                use_data_plane_v2 ? data_v2_server.GetClientCount() : data_server.GetClientCount(),
                stats.sent_bytes.load(), stats.send_fail_count.load(), dmabuf_enabled ? 1U : 0U,
                dmabuf_frames, export_failures, lease_exhausted, active_leases, lease_in_flight_max,
                min_queued, stats.v2_sent_frames.load(), stats.v2_send_fail_count.load(),
                release_server.PendingFrameCount(),
                release_server.GetServerStats().received_releases,
                release_server.GetServerStats().reclaimed_frames,
                release_server.GetServerStats().expired_reclaims,
                SourceStateName(source_status.state), source_status.disconnections,
                source_status.recovery_attempts);

            // 新增：per-stream 指标快照（调试级别）
            for (const auto& m : all_metrics)
            {
                PlatformLogger::Log(LogLevel::kDebug, "publisher", "%s",
                                    MetricsAggregator::FormatForLog(m).c_str());
            }
        }
        else
        {
            SourceStatusSnapshot source_status;
            {
                std::lock_guard<std::mutex> lock(runtimes_mutex);
                source_status = GetAggregateSourceStatus(runtimes_by_stream);
            }
            PlatformLogger::Log(
                LogLevel::kInfo, "publisher",
                "sec=%" PRIu64 " | frames=%" PRIu64 " | fps=%" PRIu64
                " | clients=%zu | sent_bytes=%" PRIu64 " | send_fail=%" PRIu64
                " | source_state=%s | disconnects=%" PRIu64 " | recovery_attempts=%" PRIu64,
                elapsed_sec, frames, fps, data_server.GetClientCount(), stats.sent_bytes.load(),
                stats.send_fail_count.load(), SourceStateName(source_status.state),
                source_status.disconnections, source_status.recovery_attempts);
        }

        // Append metrics history line every N seconds
        if (!metrics_history_path.empty() && elapsed_sec % metrics_history_interval_sec == 0)
        {
            const auto all_metrics = metrics_aggregator.GetAllStreamMetrics();
            if (!AppendMetricsLine(metrics_history_path, all_metrics, &stats, &release_server))
            {
                PlatformLogger::Log(LogLevel::kWarning, "publisher",
                                    "failed to append metrics history line to %s",
                                    metrics_history_path.c_str());
            }
        }

        if (!device_discovery_status_path.empty() &&
            elapsed_sec % metrics_history_interval_sec == 0)
        {
            std::lock_guard<std::mutex> lock(runtimes_mutex);
            if (!WriteDeviceDiscoveryStatusFile(device_discovery_status_path, runtimes_by_stream))
            {
                PlatformLogger::Log(LogLevel::kWarning, "publisher",
                                    "failed to write device discovery status to %s",
                                    device_discovery_status_path.c_str());
            }
        }
    }

    // Write final snapshot before stopping servers
    if (!metrics_snapshot_path.empty())
    {
        const auto all_metrics = metrics_aggregator.GetAllStreamMetrics();
        const std::string line = MetricsToJsonLine(all_metrics, &stats, &release_server);
        const int fd = open(metrics_snapshot_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
            if (!WriteFull(fd, line.data(), line.size()) || !WriteFull(fd, "\n", 1))
            {
                PlatformLogger::Log(LogLevel::kWarning, "publisher",
                                    "failed to write final metrics snapshot to %s",
                                    metrics_snapshot_path.c_str());
            }
            close(fd);
        }
        else
        {
            PlatformLogger::Log(LogLevel::kWarning, "publisher",
                                "failed to open metrics snapshot path %s",
                                metrics_snapshot_path.c_str());
        }
    }

    if (!device_discovery_status_path.empty())
    {
        std::lock_guard<std::mutex> lock(runtimes_mutex);
        if (!WriteDeviceDiscoveryStatusFile(device_discovery_status_path, runtimes_by_stream))
        {
            PlatformLogger::Log(LogLevel::kWarning, "publisher",
                                "failed to write final device discovery status to %s",
                                device_discovery_status_path.c_str());
        }
    }

    PlatformLogger::Log(LogLevel::kInfo, "publisher", "publisher stopping...");
    control_server.Stop();
    release_server.Stop();
    data_server.Stop();
    data_v2_server.Stop();

    {
        std::lock_guard<std::mutex> lock(runtimes_mutex);
        for (const auto& item : runtimes_by_stream)
        {
            const auto& runtime = item.second;
            if (!runtime)
            {
                continue;
            }
            runtime->source.Stop();
            {
                std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
                runtime->pending_leases.clear();
            }
        }
        runtimes_by_camera_id.clear();
        runtimes_by_stream.clear();
    }

    (void)session_manager.UnregisterCorePublisher("camera_publisher_core");
    PlatformLogger::Shutdown();
    return 0;
}
