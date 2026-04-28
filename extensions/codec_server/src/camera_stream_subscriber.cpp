#include "codec_server/camera_stream_subscriber.h"

#include "camera_subsystem/ipc/camera_control_ipc.h"
#include "camera_subsystem/ipc/camera_data_ipc.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace camera_subsystem::extensions::codec_server {
namespace {

bool WriteFull(int fd, const void* buffer, size_t length)
{
    size_t written = 0;
    const auto* data = static_cast<const uint8_t*>(buffer);
    while (written < length)
    {
        const ssize_t ret = write(fd, data + written, length - written);
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

int ConnectUnixSocket(const std::string& socket_path, int socket_type)
{
    const int fd = socket(AF_UNIX, socket_type, 0);
    if (fd < 0)
    {
        return -1;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

} // namespace

CameraStreamSubscriber::~CameraStreamSubscriber()
{
    Stop();
}

bool CameraStreamSubscriber::Start(const CameraStreamSubscriberConfig& config)
{
    if (is_running_.load())
    {
        return true;
    }

    config_ = config;
    input_frames_.store(0);
    input_bytes_.store(0);
    read_failures_.store(0);
    invalid_frames_.store(0);
    is_subscribed_.store(false);

    if (!ConnectDataSocket())
    {
        return false;
    }
    if (!ConnectControlSocket())
    {
        Stop();
        return false;
    }
    if (!SendControlRequest(
            static_cast<uint32_t>(camera_subsystem::ipc::CameraControlCommand::kSubscribe)))
    {
        Stop();
        return false;
    }
    is_subscribed_.store(true);

    is_running_.store(true);
    reader_thread_ = std::thread(&CameraStreamSubscriber::ReadLoop, this);
    return true;
}

void CameraStreamSubscriber::Stop()
{
    is_running_.store(false);
    if (data_fd_ >= 0)
    {
        shutdown(data_fd_, SHUT_RDWR);
    }
    if (is_subscribed_.exchange(false) && control_fd_ >= 0)
    {
        (void)SendControlRequest(
            static_cast<uint32_t>(camera_subsystem::ipc::CameraControlCommand::kUnsubscribe));
    }
    if (reader_thread_.joinable())
    {
        reader_thread_.join();
    }
    if (data_fd_ >= 0)
    {
        close(data_fd_);
        data_fd_ = -1;
    }
    if (control_fd_ >= 0)
    {
        close(control_fd_);
        control_fd_ = -1;
    }
}

bool CameraStreamSubscriber::IsRunning() const
{
    return is_running_.load();
}

CameraStreamSubscriberStats CameraStreamSubscriber::GetStats() const
{
    CameraStreamSubscriberStats stats;
    stats.input_frames = input_frames_.load();
    stats.input_bytes = input_bytes_.load();
    stats.read_failures = read_failures_.load();
    stats.invalid_frames = invalid_frames_.load();
    return stats;
}

bool CameraStreamSubscriber::ConnectDataSocket()
{
    data_fd_ = ConnectUnixSocket(config_.data_socket, SOCK_STREAM);
    return data_fd_ >= 0;
}

bool CameraStreamSubscriber::ConnectControlSocket()
{
    control_fd_ = ConnectUnixSocket(config_.control_socket, SOCK_STREAM);
    return control_fd_ >= 0;
}

bool CameraStreamSubscriber::SendControlRequest(uint32_t command)
{
    using namespace camera_subsystem::ipc;

    if (control_fd_ < 0)
    {
        return false;
    }

    const CameraEndpoint endpoint =
        MakeCameraEndpoint(config_.camera_id,
                           CameraBusType::kDefault,
                           0,
                           config_.device_path.c_str());
    const auto request =
        MakeControlRequest(static_cast<CameraControlCommand>(command),
                           CameraClientRole::kSubscriber,
                           endpoint,
                           config_.client_id.c_str());
    if (!WriteFull(control_fd_, &request, sizeof(request)))
    {
        return false;
    }

    CameraControlResponse response;
    if (!ReadFull(control_fd_, &response, sizeof(response)))
    {
        return false;
    }
    return IsControlResponseHeaderValid(response) &&
           response.status == CameraControlStatus::kOk;
}

void CameraStreamSubscriber::ReadLoop()
{
    using namespace camera_subsystem::ipc;

    while (is_running_.load())
    {
        CameraDataFrameHeader header;
        if (!ReadFull(data_fd_, &header, sizeof(header)))
        {
            if (is_running_.load())
            {
                read_failures_.fetch_add(1);
            }
            break;
        }

        if (!IsCameraDataFrameHeaderValid(header) ||
            header.frame_size > config_.max_frame_size)
        {
            invalid_frames_.fetch_add(1);
            break;
        }

        std::vector<uint8_t> payload(header.frame_size);
        if (!ReadFull(data_fd_, payload.data(), payload.size()))
        {
            if (is_running_.load())
            {
                read_failures_.fetch_add(1);
            }
            break;
        }

        input_frames_.fetch_add(1);
        input_bytes_.fetch_add(header.frame_size);
        if (config_.frame_callback)
        {
            config_.frame_callback(header, payload);
        }
    }
    is_running_.store(false);
}

bool CameraStreamSubscriber::ReadFull(int fd, void* buffer, size_t length)
{
    size_t total = 0;
    auto* out = static_cast<uint8_t*>(buffer);
    while (total < length)
    {
        const ssize_t ret = read(fd, out + total, length - total);
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
        total += static_cast<size_t>(ret);
    }
    return true;
}

} // namespace camera_subsystem::extensions::codec_server
