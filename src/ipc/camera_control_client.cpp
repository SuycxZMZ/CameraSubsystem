/**
 * @file camera_control_client.cpp
 * @brief Camera 控制面客户端实现
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#include "camera_subsystem/ipc/camera_control_client.h"

#include "camera_subsystem/platform/platform_logger.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace camera_subsystem
{
namespace ipc
{

CameraControlClient::CameraControlClient()
    : socket_fd_(-1)
    , socket_path_()
{
}

CameraControlClient::~CameraControlClient()
{
    Disconnect();
}

bool CameraControlClient::Connect(const std::string& socket_path)
{
    if (socket_fd_ >= 0)
    {
        return true;
    }

    if (socket_path.empty())
    {
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_control_client",
                                      "socket failed: %s", strerror(errno));
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_control_client",
                                      "connect failed: path=%s err=%s", socket_path.c_str(),
                                      strerror(errno));
        close(fd);
        return false;
    }

    socket_fd_ = fd;
    socket_path_ = socket_path;
    return true;
}

void CameraControlClient::Disconnect()
{
    if (socket_fd_ >= 0)
    {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    socket_path_.clear();
}

bool CameraControlClient::IsConnected() const
{
    return socket_fd_ >= 0;
}

bool CameraControlClient::Subscribe(const std::string& client_id,
                                    CameraClientRole role,
                                    const CameraEndpoint& endpoint,
                                    CameraControlResponse* response)
{
    const CameraControlRequest request =
        MakeControlRequest(CameraControlCommand::kSubscribe, role, endpoint, client_id.c_str());
    return SendRequest(request, response);
}

bool CameraControlClient::Unsubscribe(const std::string& client_id,
                                      CameraClientRole role,
                                      const CameraEndpoint& endpoint,
                                      CameraControlResponse* response)
{
    const CameraControlRequest request =
        MakeControlRequest(CameraControlCommand::kUnsubscribe, role, endpoint, client_id.c_str());
    return SendRequest(request, response);
}

bool CameraControlClient::Ping(CameraControlResponse* response)
{
    const CameraEndpoint endpoint = MakeDefaultCameraEndpoint(0);
    const CameraControlRequest request =
        MakeControlRequest(CameraControlCommand::kPing, CameraClientRole::kSubscriber, endpoint,
                           "ping_client");
    return SendRequest(request, response);
}

bool CameraControlClient::SendRequest(const CameraControlRequest& request,
                                      CameraControlResponse* response)
{
    if (socket_fd_ < 0)
    {
        return false;
    }

    if (!WriteFull(socket_fd_, &request, sizeof(request)))
    {
        return false;
    }

    CameraControlResponse local_response;
    if (!ReadFull(socket_fd_, &local_response, sizeof(local_response)))
    {
        return false;
    }

    if (response != nullptr)
    {
        *response = local_response;
    }

    return IsControlResponseHeaderValid(local_response) &&
           local_response.status == CameraControlStatus::kOk;
}

bool CameraControlClient::ReadFull(int fd, void* buffer, size_t length)
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

bool CameraControlClient::WriteFull(int fd, const void* buffer, size_t length)
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

} // namespace ipc
} // namespace camera_subsystem
