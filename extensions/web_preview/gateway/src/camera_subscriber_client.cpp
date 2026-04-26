#include "web_preview/camera_subscriber_client.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "camera_subsystem/ipc/camera_control_ipc.h"

namespace web_preview {
namespace {

constexpr uint32_t kMaxFrameSize = 64U * 1024U * 1024U;

void CloseFd(int* fd)
{
    if (fd && *fd >= 0)
    {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

} // namespace

CameraSubscriberClient::CameraSubscriberClient()
    : running_(false)
    , control_fd_(-1)
    , data_fd_(-1)
{
}

CameraSubscriberClient::~CameraSubscriberClient()
{
    Stop();
}

bool CameraSubscriberClient::Start(const GatewayConfig& config,
                                   FrameCallback frame_callback,
                                   StatusCallback status_callback)
{
    if (running_.load())
    {
        return true;
    }

    config_ = config;
    frame_callback_ = std::move(frame_callback);
    status_callback_ = std::move(status_callback);

    if (!ConnectData())
    {
        return false;
    }
    if (!ConnectControl())
    {
        CloseFd(&data_fd_);
        return false;
    }
    if (!Subscribe())
    {
        CloseFd(&control_fd_);
        CloseFd(&data_fd_);
        return false;
    }

    running_.store(true);
    read_thread_ = std::thread(&CameraSubscriberClient::ReadLoop, this);
    return true;
}

void CameraSubscriberClient::Stop()
{
    const bool was_running = running_.exchange(false);

    if (was_running)
    {
        Unsubscribe();
    }
    CloseFd(&control_fd_);
    CloseFd(&data_fd_);

    if (read_thread_.joinable())
    {
        read_thread_.join();
    }
}

bool CameraSubscriberClient::IsRunning() const
{
    return running_.load();
}

bool CameraSubscriberClient::ConnectControl()
{
    control_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control_fd_ < 0)
    {
        std::cerr << "control socket failed: " << strerror(errno) << "\n";
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, config_.control_socket.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(control_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "connect control socket failed: " << config_.control_socket
                  << " err=" << strerror(errno) << "\n";
        CloseFd(&control_fd_);
        return false;
    }

    return true;
}

bool CameraSubscriberClient::ConnectData()
{
    data_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (data_fd_ < 0)
    {
        std::cerr << "data socket failed: " << strerror(errno) << "\n";
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, config_.data_socket.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(data_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "connect data socket failed: " << config_.data_socket
                  << " err=" << strerror(errno) << "\n";
        CloseFd(&data_fd_);
        return false;
    }

    return true;
}

bool CameraSubscriberClient::Subscribe()
{
    std::string message;
    const bool ok = SendControlRequest(
        static_cast<uint32_t>(camera_subsystem::ipc::CameraControlCommand::kSubscribe), &message);
    if (status_callback_)
    {
        status_callback_(ok ? "subscribed" : ("subscribe_failed: " + message));
    }
    return ok;
}

void CameraSubscriberClient::Unsubscribe()
{
    if (control_fd_ < 0)
    {
        return;
    }
    std::string message;
    (void)SendControlRequest(
        static_cast<uint32_t>(camera_subsystem::ipc::CameraControlCommand::kUnsubscribe),
        &message);
}

bool CameraSubscriberClient::SendControlRequest(uint32_t command, std::string* message)
{
    using namespace camera_subsystem::ipc;

    if (control_fd_ < 0)
    {
        if (message)
        {
            *message = "control socket not connected";
        }
        return false;
    }

    const CameraEndpoint endpoint = MakeCameraEndpoint(config_.camera_id,
                                                       CameraBusType::kDefault,
                                                       0,
                                                       config_.device_path.c_str());
    const CameraControlRequest request = MakeControlRequest(
        static_cast<CameraControlCommand>(command),
        CameraClientRole::kSubscriber,
        endpoint,
        config_.client_id.c_str());

    if (!WriteFull(control_fd_, &request, sizeof(request)))
    {
        if (message)
        {
            *message = "write control request failed";
        }
        return false;
    }

    CameraControlResponse response;
    if (!ReadFull(control_fd_, &response, sizeof(response)))
    {
        if (message)
        {
            *message = "read control response failed";
        }
        return false;
    }

    if (message)
    {
        *message = response.message;
    }

    return IsControlResponseHeaderValid(response) && response.status == CameraControlStatus::kOk;
}

void CameraSubscriberClient::ReadLoop()
{
    while (running_.load())
    {
        camera_subsystem::ipc::CameraDataFrameHeader header;
        if (!ReadFull(data_fd_, &header, sizeof(header)))
        {
            if (running_.load() && status_callback_)
            {
                status_callback_("data_channel_closed");
            }
            break;
        }

        if (!camera_subsystem::ipc::IsCameraDataFrameHeaderValid(header) ||
            header.frame_size > kMaxFrameSize)
        {
            if (status_callback_)
            {
                status_callback_("invalid_frame_header");
            }
            break;
        }

        CameraFrame frame;
        frame.header = header;
        frame.payload.resize(header.frame_size);

        if (!ReadFull(data_fd_, frame.payload.data(), frame.payload.size()))
        {
            if (running_.load() && status_callback_)
            {
                status_callback_("data_payload_read_failed");
            }
            break;
        }

        if (frame_callback_)
        {
            frame_callback_(std::move(frame));
        }
    }

    running_.store(false);
}

bool CameraSubscriberClient::ReadFull(int fd, void* buffer, size_t length)
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

bool CameraSubscriberClient::WriteFull(int fd, const void* buffer, size_t length)
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

} // namespace web_preview
