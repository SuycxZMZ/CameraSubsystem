#include "detection_server/detection_control_server.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace camera_subsystem::extensions::detection_server {

DetectionControlServer::~DetectionControlServer()
{
    Stop();
}

bool DetectionControlServer::Start(const std::string& socket_path, RequestHandler request_handler)
{
    if (is_running_.load())
    {
        return true;
    }
    if (socket_path.empty() || !request_handler)
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
    if (listen(fd, 8) < 0)
    {
        close(fd);
        unlink(socket_path.c_str());
        return false;
    }

    listen_fd_ = fd;
    socket_path_ = socket_path;
    request_handler_ = std::move(request_handler);
    is_running_.store(true);
    accept_thread_ = std::thread(&DetectionControlServer::AcceptLoop, this);
    return true;
}

void DetectionControlServer::Stop()
{
    if (!is_running_.exchange(false))
    {
        return;
    }

    if (listen_fd_ >= 0)
    {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
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

    if (!socket_path_.empty())
    {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

bool DetectionControlServer::IsRunning() const
{
    return is_running_.load();
}

void DetectionControlServer::AcceptLoop()
{
    while (is_running_.load())
    {
        const int client_fd = accept(listen_fd_, nullptr, nullptr);
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
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.push_back(client_fd);
        }
        client_threads_.emplace_back(&DetectionControlServer::ClientLoop, this, client_fd);
    }
}

void DetectionControlServer::ClientLoop(int client_fd)
{
    while (is_running_.load())
    {
        std::string line;
        if (!ReadLine(client_fd, &line))
        {
            break;
        }

        std::string response = request_handler_ ? request_handler_(line) : std::string();
        if (response.empty())
        {
            response = "{\"type\":\"detection_response\",\"ok\":false,"
                       "\"error_code\":\"INVALID_CONFIG\",\"message\":\"empty response\"}\n";
        }
        if (!WriteFull(client_fd, response.data(), response.size()))
        {
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find(client_fds_.begin(), client_fds_.end(), client_fd);
        if (it != client_fds_.end())
        {
            close(*it);
            client_fds_.erase(it);
        }
    }
}

bool DetectionControlServer::ReadLine(int fd, std::string* line)
{
    if (!line)
    {
        return false;
    }
    line->clear();

    char ch = '\0';
    while (true)
    {
        const ssize_t ret = read(fd, &ch, 1);
        if (ret == 0)
        {
            return false;
        }
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (ch == '\n')
        {
            return true;
        }
        line->push_back(ch);
    }
}

bool DetectionControlServer::WriteFull(int fd, const void* buffer, size_t length)
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

} // namespace camera_subsystem::extensions::detection_server
