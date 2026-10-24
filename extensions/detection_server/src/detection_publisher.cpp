#include "detection_server/detection_publisher.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace camera_subsystem::extensions::detection_server {
namespace {

std::string JsonEscape(const std::string& value)
{
    std::ostringstream stream;
    for (char ch : value)
    {
        switch (ch)
        {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                              static_cast<unsigned char>(ch));
                stream << buffer;
            }
            else
            {
                stream << ch;
            }
            break;
        }
    }
    return stream.str();
}

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

} // namespace

DetectionPublisher::~DetectionPublisher()
{
    Stop();
}

bool DetectionPublisher::Start(const DetectionPublisherConfig& config)
{
    if (is_running_.load())
    {
        return true;
    }

    config_ = config;
    published_results_.store(0);
    publish_failures_.store(0);
    dropped_clients_.store(0);

    unlink(config_.result_socket.c_str());
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, config_.result_socket.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (listen(listen_fd_, 8) < 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    is_running_.store(true);
    accept_thread_ = std::thread(&DetectionPublisher::AcceptLoop, this);
    return true;
}

void DetectionPublisher::Stop()
{
    is_running_.store(false);
    if (listen_fd_ >= 0)
    {
        shutdown(listen_fd_, SHUT_RDWR);
    }
    if (accept_thread_.joinable())
    {
        accept_thread_.join();
    }
    if (listen_fd_ >= 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    CloseAllClients();
    if (!config_.result_socket.empty())
    {
        unlink(config_.result_socket.c_str());
    }
}

bool DetectionPublisher::PublishResult(const DetectionResult& result)
{
    const std::string json_line = SerializeDetectionResultJsonLine(result);
    std::lock_guard<std::mutex> lock(clients_mutex_);

    std::vector<int> alive_clients;
    alive_clients.reserve(client_fds_.size());
    bool all_written = true;

    for (int client_fd : client_fds_)
    {
        if (!WriteFull(client_fd, json_line.data(), json_line.size()))
        {
            close(client_fd);
            dropped_clients_.fetch_add(1);
            publish_failures_.fetch_add(1);
            all_written = false;
            continue;
        }
        alive_clients.push_back(client_fd);
    }

    client_fds_.swap(alive_clients);
    published_results_.fetch_add(1);
    return all_written;
}

DetectionPublisherStats DetectionPublisher::GetStats() const
{
    DetectionPublisherStats stats;
    stats.published_results = published_results_.load();
    stats.publish_failures = publish_failures_.load();
    stats.dropped_clients = dropped_clients_.load();
    std::lock_guard<std::mutex> lock(clients_mutex_);
    stats.connected_clients = client_fds_.size();
    return stats;
}

bool DetectionPublisher::IsRunning() const
{
    return is_running_.load();
}

std::string DetectionPublisher::SerializeDetectionResultJsonLine(const DetectionResult& result)
{
    std::ostringstream stream;
    stream << "{"
           << "\"type\":\"detection_result\","
           << "\"stream_id\":\"" << JsonEscape(result.stream_id) << "\","
           << "\"frame_id\":" << result.frame_id << ","
           << "\"timestamp_ns\":" << result.timestamp_ns << ","
           << "\"model\":{"
           << "\"name\":\"" << JsonEscape(result.model_name) << "\","
           << "\"runtime\":\"rknnrt\","
           << "\"runtime_version\":\"" << JsonEscape(result.runtime_version) << "\","
           << "\"driver_version\":\"" << JsonEscape(result.driver_version) << "\","
           << "\"core_mask\":" << result.npu_core_mask << "},"
           << "\"image\":{"
           << "\"width\":" << result.image_width << ","
           << "\"height\":" << result.image_height << ","
           << "\"letterbox_width\":" << result.letterbox_width << ","
           << "\"letterbox_height\":" << result.letterbox_height << "},"
           << "\"metrics\":{"
           << "\"preprocess_ms\":" << result.preprocess_ms << ","
           << "\"inference_ms\":" << result.inference_ms << ","
           << "\"postprocess_ms\":" << result.postprocess_ms << ","
           << "\"total_ms\":" << result.total_ms << "},"
           << "\"objects\":[";

    for (size_t index = 0; index < result.objects.size(); ++index)
    {
        const DetectionBox& box = result.objects[index];
        if (index > 0)
        {
            stream << ",";
        }
        stream << "{"
               << "\"class_id\":" << box.class_id << ","
               << "\"label\":\"" << JsonEscape(box.label) << "\","
               << "\"score\":" << box.score << ","
               << "\"bbox_xyxy\":[" << box.x1 << "," << box.y1 << "," << box.x2 << "," << box.y2
               << "],"
               << "\"bbox_norm_xyxy\":[" << box.nx1 << "," << box.ny1 << "," << box.nx2 << ","
               << box.ny2 << "]"
               << "}";
    }

    stream << "]}\n";
    return stream.str();
}

void DetectionPublisher::AcceptLoop()
{
    while (is_running_.load())
    {
        const int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_fds_.push_back(client_fd);
    }
}

void DetectionPublisher::CloseAllClients()
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int client_fd : client_fds_)
    {
        close(client_fd);
    }
    client_fds_.clear();
}

} // namespace camera_subsystem::extensions::detection_server
