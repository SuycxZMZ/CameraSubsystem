#include "detection_server/camera_frame_subscriber.h"

#include "camera_subsystem/ipc/camera_control_ipc.h"
#include "camera_subsystem/ipc/camera_data_ipc.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace camera_subsystem::extensions::detection_server {
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

const char* ControlStatusToString(
    camera_subsystem::ipc::CameraControlStatus status)
{
    using camera_subsystem::ipc::CameraControlStatus;
    switch (status)
    {
    case CameraControlStatus::kOk:
        return "ok";
    case CameraControlStatus::kInvalidMessage:
        return "invalid_message";
    case CameraControlStatus::kInvalidRole:
        return "invalid_role";
    case CameraControlStatus::kCorePublisherUnavailable:
        return "core_publisher_unavailable";
    case CameraControlStatus::kSessionOperationFailed:
        return "session_operation_failed";
    default:
        return "unknown";
    }
}

} // namespace

CameraFrameSubscriber::~CameraFrameSubscriber()
{
    Stop();
}

bool CameraFrameSubscriber::Start(const CameraFrameSubscriberConfig& config)
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
    SetLastError(std::string(), 0, std::string());

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
    reader_thread_ = std::thread(&CameraFrameSubscriber::ReadLoop, this);
    return true;
}

void CameraFrameSubscriber::Stop()
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

bool CameraFrameSubscriber::IsRunning() const
{
    return is_running_.load();
}

CameraFrameSubscriberStats CameraFrameSubscriber::GetStats() const
{
    CameraFrameSubscriberStats stats;
    stats.input_frames = input_frames_.load();
    stats.input_bytes = input_bytes_.load();
    stats.read_failures = read_failures_.load();
    stats.invalid_frames = invalid_frames_.load();
    return stats;
}

int CameraFrameSubscriber::GetLastErrorNo() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_no_;
}

std::string CameraFrameSubscriber::GetLastErrorStage() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_stage_;
}

std::string CameraFrameSubscriber::GetLastErrorMessage() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_message_;
}

bool CameraFrameSubscriber::ConnectDataSocket()
{
    data_fd_ = ConnectUnixSocket(config_.data_socket, SOCK_STREAM);
    if (data_fd_ < 0)
    {
        SetLastError("connect_data", errno,
                     "failed to connect data socket: " + config_.data_socket +
                         " err=" + std::strerror(errno));
    }
    return data_fd_ >= 0;
}

bool CameraFrameSubscriber::ConnectControlSocket()
{
    control_fd_ = ConnectUnixSocket(config_.control_socket, SOCK_STREAM);
    if (control_fd_ < 0)
    {
        SetLastError("connect_control", errno,
                     "failed to connect control socket: " + config_.control_socket +
                         " err=" + std::strerror(errno));
    }
    return control_fd_ >= 0;
}

bool CameraFrameSubscriber::SendControlRequest(uint32_t command)
{
    using namespace camera_subsystem::ipc;

    if (control_fd_ < 0)
    {
        SetLastError("control_precheck", 0, "control socket is not connected");
        return false;
    }

    CameraEndpoint endpoint = MakeCameraEndpoint(config_.camera_id, CameraBusType::kDefault, 0,
                                                 config_.device_path.c_str());
    SetEndpointStreamId(&endpoint, config_.stream_id.c_str());
    const auto request =
        MakeControlRequest(static_cast<CameraControlCommand>(command),
                           CameraClientRole::kSubscriber, endpoint, config_.client_id.c_str());
    if (!WriteFull(control_fd_, &request, sizeof(request)))
    {
        SetLastError("control_write", errno,
                     "failed to write control request err=" + std::string(std::strerror(errno)));
        return false;
    }

    CameraControlResponse response;
    if (!ReadFull(control_fd_, &response, sizeof(response)))
    {
        SetLastError("control_read", errno,
                     "failed to read control response err=" + std::string(std::strerror(errno)));
        return false;
    }
    if (!IsControlResponseHeaderValid(response))
    {
        SetLastError("control_response", 0, "invalid control response header");
        return false;
    }
    if (response.status != CameraControlStatus::kOk)
    {
        SetLastError("control_response", static_cast<int>(response.status),
                     "control response status=" +
                         std::string(ControlStatusToString(response.status)) +
                         " message=" + response.message);
        return false;
    }
    return true;
}

void CameraFrameSubscriber::ReadLoop()
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

        if (!IsCameraDataFrameHeaderValid(header) || header.frame_size > config_.max_frame_size)
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

void CameraFrameSubscriber::SetLastError(const std::string& stage,
                                         int error_no,
                                         const std::string& message)
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_stage_ = stage;
    last_error_no_ = error_no;
    last_error_message_ = message;
}

bool CameraFrameSubscriber::ReadFull(int fd, void* buffer, size_t length)
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

} // namespace camera_subsystem::extensions::detection_server
