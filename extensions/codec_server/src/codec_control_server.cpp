#include "codec_server/codec_control_server.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace camera_subsystem::extensions::codec_server {
namespace {

bool WriteAll(int fd, const std::string& data)
{
    size_t written = 0;
    while (written < data.size())
    {
        const ssize_t ret = write(fd, data.data() + written, data.size() - written);
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

CodecControlStatus MakeErrorStatus(const CodecControlRequest& request,
                                   const std::string& error)
{
    CodecControlStatus status;
    status.request_id = request.request_id;
    status.stream_id = request.stream_id;
    status.recording = false;
    status.state = "error";
    status.error = error;
    return status;
}

} // namespace

CodecControlServer::CodecControlServer(RecordingSessionManager* session_manager)
    : session_manager_(session_manager)
{
}

CodecControlServer::~CodecControlServer()
{
    Stop();
}

bool CodecControlServer::Start(const std::string& socket_path)
{
    if (!session_manager_ || is_running_.load())
    {
        return session_manager_ != nullptr;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::cerr << "codec control socket create failed: " << strerror(errno) << "\n";
        return false;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    unlink(socket_path.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "codec control socket bind failed: path=" << socket_path
                  << " err=" << strerror(errno) << "\n";
        close(fd);
        return false;
    }

    if (listen(fd, 16) < 0)
    {
        std::cerr << "codec control socket listen failed: " << strerror(errno) << "\n";
        close(fd);
        unlink(socket_path.c_str());
        return false;
    }

    server_fd_ = fd;
    socket_path_ = socket_path;
    is_running_.store(true);
    accept_thread_ = std::thread(&CodecControlServer::AcceptLoop, this);
    return true;
}

void CodecControlServer::Stop()
{
    if (!is_running_.exchange(false))
    {
        return;
    }

    if (server_fd_ >= 0)
    {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (accept_thread_.joinable())
    {
        accept_thread_.join();
    }

    std::vector<std::thread> client_threads;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const int client_fd : client_fds_)
        {
            shutdown(client_fd, SHUT_RDWR);
        }
        client_threads = std::move(client_threads_);
        client_fds_.clear();
    }
    for (auto& thread : client_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    if (!socket_path_.empty())
    {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

bool CodecControlServer::IsRunning() const
{
    return is_running_.load();
}

void CodecControlServer::AcceptLoop()
{
    while (is_running_.load())
    {
        const int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0)
        {
            if (!is_running_.load() || errno == EBADF || errno == EINVAL)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            std::cerr << "codec control accept failed: " << strerror(errno) << "\n";
            continue;
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_fds_.push_back(client_fd);
        client_threads_.emplace_back(&CodecControlServer::ClientLoop, this, client_fd);
    }
}

void CodecControlServer::ClientLoop(int client_fd)
{
    std::string line;
    char ch = 0;
    while (is_running_.load())
    {
        const ssize_t ret = read(client_fd, &ch, 1);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (ret == 0)
        {
            break;
        }

        if (ch == '\n')
        {
            CodecControlRequest request;
            std::string error;
            CodecControlStatus status;
            if (ParseCodecControlRequestLine(line, &request, &error))
            {
                status = HandleRequest(request);
            }
            else
            {
                status = MakeErrorStatus(request, error);
            }

            const std::string response = SerializeCodecControlStatus(status) + "\n";
            if (!WriteAll(client_fd, response))
            {
                break;
            }
            line.clear();
            continue;
        }

        if (line.size() >= 64U * 1024U)
        {
            CodecControlRequest request;
            CodecControlStatus status = MakeErrorStatus(request, "request_too_large");
            const std::string response = SerializeCodecControlStatus(status) + "\n";
            (void)WriteAll(client_fd, response);
            line.clear();
            continue;
        }
        line.push_back(ch);
    }

    close(client_fd);
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(client_fds_.begin(), client_fds_.end(), client_fd);
    if (it != client_fds_.end())
    {
        client_fds_.erase(it);
    }
}

CodecControlStatus CodecControlServer::HandleRequest(
    const CodecControlRequest& request)
{
    switch (request.command)
    {
    case CodecControlCommand::kStartRecording:
        return session_manager_->StartRecording(request);
    case CodecControlCommand::kStopRecording:
        return session_manager_->StopRecording(request);
    case CodecControlCommand::kStatus:
        return session_manager_->GetStatus(request);
    case CodecControlCommand::kUnknown:
        break;
    }
    return MakeErrorStatus(request, "unknown_command");
}

} // namespace camera_subsystem::extensions::codec_server
