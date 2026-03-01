/**
 * @file camera_control_server.cpp
 * @brief Camera 控制面服务端实现
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#include "camera_subsystem/ipc/camera_control_server.h"

#include "camera_subsystem/platform/platform_logger.h"

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

} // namespace

CameraControlServer::CameraControlServer(camera::CameraSessionManager* session_manager)
    : session_manager_(session_manager)
    , server_fd_(-1)
    , socket_path_()
    , is_running_(false)
    , accept_thread_()
    , clients_mutex_()
    , client_fds_()
    , client_subscriptions_()
    , client_threads_()
    , error_mutex_()
    , last_error_no_(0)
    , last_error_stage_()
    , last_error_message_()
{
}

CameraControlServer::~CameraControlServer()
{
    Stop();
}

bool CameraControlServer::Start(const std::string& socket_path)
{
    if (is_running_.load())
    {
        return true;
    }

    SetLastError(std::string(), 0, std::string());

    if (session_manager_ == nullptr)
    {
        SetLastError("precheck", 0, "session_manager is null");
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_control_server",
                                      "Start failed: session_manager is null");
        return false;
    }

    if (socket_path.empty() || socket_path.size() >= kUnixSocketPathMaxLength)
    {
        SetLastError("precheck", 0, "invalid socket_path");
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_control_server",
                                      "Start failed: invalid socket_path=%s",
                                      socket_path.c_str());
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        const int error_no = errno;
        SetLastError("socket", error_no, strerror(error_no));
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_control_server",
                                      "socket failed: %s", strerror(errno));
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    unlink(socket_path.c_str());

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        const int error_no = errno;
        SetLastError("bind", error_no, strerror(error_no));
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_control_server",
                                      "bind failed: path=%s err=%s", socket_path.c_str(),
                                      strerror(errno));
        close(fd);
        return false;
    }

    if (listen(fd, 16) < 0)
    {
        const int error_no = errno;
        SetLastError("listen", error_no, strerror(error_no));
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_control_server",
                                      "listen failed: %s", strerror(errno));
        close(fd);
        unlink(socket_path.c_str());
        return false;
    }

    // 避免 Stop 时 accept 阻塞导致退出卡住
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
    {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    server_fd_ = fd;
    socket_path_ = socket_path;
    is_running_.store(true);
    accept_thread_ = std::thread(&CameraControlServer::AcceptLoop, this);
    return true;
}

void CameraControlServer::Stop()
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

    for (auto& thread : client_threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    client_threads_.clear();

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_subscriptions_.clear();
    }

    if (!socket_path_.empty())
    {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

bool CameraControlServer::IsRunning() const
{
    return is_running_.load();
}

std::string CameraControlServer::GetSocketPath() const
{
    return socket_path_;
}

int CameraControlServer::GetLastErrorNo() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_no_;
}

std::string CameraControlServer::GetLastErrorStage() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_stage_;
}

std::string CameraControlServer::GetLastErrorMessage() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_message_;
}

void CameraControlServer::AcceptLoop()
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

            platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_control_server",
                                          "accept failed: %s", strerror(errno));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.insert(client_fd);
            client_subscriptions_[client_fd] = std::vector<ClientSubscription>();
        }

        client_threads_.emplace_back(&CameraControlServer::ClientLoop, this, client_fd);
    }
}

void CameraControlServer::ClientLoop(int client_fd)
{
    while (is_running_.load())
    {
        CameraControlRequest request;
        if (!ReadFull(client_fd, &request, sizeof(request)))
        {
            break;
        }

        const CameraControlResponse response = ProcessRequest(client_fd, request);
        if (!WriteFull(client_fd, &response, sizeof(response)))
        {
            break;
        }
    }

    CleanupClientSubscriptions(client_fd);

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_subscriptions_.erase(client_fd);
        auto it = client_fds_.find(client_fd);
        if (it != client_fds_.end())
        {
            close(*it);
            client_fds_.erase(it);
        }
    }
}

void CameraControlServer::CleanupClientSubscriptions(int client_fd)
{
    std::vector<ClientSubscription> subscriptions;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = client_subscriptions_.find(client_fd);
        if (it != client_subscriptions_.end())
        {
            subscriptions = it->second;
        }
    }

    for (const ClientSubscription& item : subscriptions)
    {
        if (session_manager_)
        {
            session_manager_->Unsubscribe(item.client_id, item.endpoint);
        }
    }
}

CameraControlResponse CameraControlServer::ProcessRequest(int client_fd,
                                                          const CameraControlRequest& request)
{
    if (!IsControlRequestHeaderValid(request))
    {
        return MakeControlResponse(CameraControlStatus::kInvalidMessage, 0,
                                   "invalid request header");
    }

    if (request.client_id[sizeof(request.client_id) - 1] != '\0')
    {
        return MakeControlResponse(CameraControlStatus::kInvalidMessage, 0,
                                   "client_id is not null terminated");
    }

    const std::string client_id = request.client_id;

    if (request.command == CameraControlCommand::kPing)
    {
        return MakeControlResponse(CameraControlStatus::kOk, 0, "pong");
    }

    if (request.role == CameraClientRole::kCorePublisher)
    {
        return MakeControlResponse(CameraControlStatus::kInvalidRole, 0,
                                   "core publisher role is not allowed in control client");
    }

    if (!session_manager_ || !session_manager_->IsCorePublisherRegistered())
    {
        return MakeControlResponse(CameraControlStatus::kCorePublisherUnavailable, 0,
                                   "core publisher is unavailable");
    }

    CameraEndpoint endpoint = request.endpoint;
    endpoint.device_path[sizeof(endpoint.device_path) - 1] = '\0';

    bool ok = false;
    if (request.command == CameraControlCommand::kSubscribe)
    {
        ok = session_manager_->Subscribe(client_id, request.role, endpoint);
        if (ok)
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            AddClientSubscriptionLocked(client_fd, client_id, endpoint);
        }
    }
    else if (request.command == CameraControlCommand::kUnsubscribe)
    {
        ok = session_manager_->Unsubscribe(client_id, endpoint);
        if (ok)
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            RemoveClientSubscriptionLocked(client_fd, client_id, endpoint);
        }
    }
    else
    {
        return MakeControlResponse(CameraControlStatus::kInvalidMessage, 0,
                                   "unsupported command");
    }

    if (!ok)
    {
        return MakeControlResponse(CameraControlStatus::kSessionOperationFailed, 0,
                                   "session operation failed");
    }

    const uint32_t active_count = session_manager_->GetSubscriberCount(endpoint);
    return MakeControlResponse(CameraControlStatus::kOk, active_count, "ok");
}

bool CameraControlServer::AddClientSubscriptionLocked(int client_fd,
                                                      const std::string& client_id,
                                                      const CameraEndpoint& endpoint)
{
    auto it = client_subscriptions_.find(client_fd);
    if (it == client_subscriptions_.end())
    {
        return false;
    }

    std::vector<ClientSubscription>& subscriptions = it->second;
    for (const ClientSubscription& item : subscriptions)
    {
        if (item.client_id == client_id && EndpointEquals(item.endpoint, endpoint))
        {
            return true;
        }
    }

    ClientSubscription subscription;
    subscription.client_id = client_id;
    subscription.endpoint = endpoint;
    subscriptions.push_back(subscription);
    return true;
}

void CameraControlServer::RemoveClientSubscriptionLocked(int client_fd,
                                                         const std::string& client_id,
                                                         const CameraEndpoint& endpoint)
{
    auto it = client_subscriptions_.find(client_fd);
    if (it == client_subscriptions_.end())
    {
        return;
    }

    std::vector<ClientSubscription>& subscriptions = it->second;
    subscriptions.erase(
        std::remove_if(subscriptions.begin(), subscriptions.end(),
                       [&](const ClientSubscription& item)
                       {
                           return item.client_id == client_id &&
                                  EndpointEquals(item.endpoint, endpoint);
                       }),
        subscriptions.end());
}

bool CameraControlServer::EndpointEquals(const CameraEndpoint& lhs, const CameraEndpoint& rhs)
{
    return lhs.camera_id == rhs.camera_id &&
           lhs.bus_type == rhs.bus_type &&
           lhs.bus_index == rhs.bus_index &&
           std::strncmp(lhs.device_path, rhs.device_path, sizeof(lhs.device_path)) == 0;
}

bool CameraControlServer::ReadFull(int fd, void* buffer, size_t length)
{
    size_t total = 0;
    auto* out = reinterpret_cast<uint8_t*>(buffer);
    while (total < length)
    {
        const ssize_t n = read(fd, out + total, length - total);
        if (n == 0)
        {
            return false;
        }
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

bool CameraControlServer::WriteFull(int fd, const void* buffer, size_t length)
{
    size_t total = 0;
    const auto* in = reinterpret_cast<const uint8_t*>(buffer);
    while (total < length)
    {
        const ssize_t n = write(fd, in + total, length - total);
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

void CameraControlServer::SetLastError(const std::string& stage,
                                       int error_no,
                                       const std::string& message)
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_stage_ = stage;
    last_error_no_ = error_no;
    last_error_message_ = message;
}

} // namespace ipc
} // namespace camera_subsystem
