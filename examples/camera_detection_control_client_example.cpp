/**
 * @file camera_detection_control_client_example.cpp
 * @brief Detection 控制面调试客户端
 */

#include "camera_subsystem/platform/platform_logger.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using camera_subsystem::core::LogLevel;
using camera_subsystem::platform::PlatformLogger;

namespace {

int ConnectUnixSocket(const std::string& socket_path)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
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

bool WriteFull(int fd, const void* buffer, size_t length)
{
    size_t total = 0;
    const auto* data = static_cast<const uint8_t*>(buffer);
    while (total < length)
    {
        const ssize_t ret = write(fd, data + total, length - total);
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

bool ReadLine(int fd, std::string* line)
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

void PrintUsage(const char* program)
{
    PlatformLogger::Log(
        LogLevel::kInfo, "detection_control_client",
        "usage: %s [--control-socket path] [--request json_line_without_newline]", program);
}

} // namespace

int main(int argc, char* argv[])
{
    std::string control_socket = "/tmp/camera_subsystem_detection.sock";
    std::string request =
        "{\"type\":\"get_detection_status\",\"request_id\":\"req-status-1\","
        "\"stream_id\":\"default0\"}";

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--control-socket" && i + 1 < argc)
        {
            control_socket = argv[++i];
        }
        else if (arg == "--request" && i + 1 < argc)
        {
            request = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
            {
                return 1;
            }
            PrintUsage(argv[0]);
            PlatformLogger::Shutdown();
            return 0;
        }
        else
        {
            if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
            {
                return 1;
            }
            PlatformLogger::Log(LogLevel::kError, "detection_control_client",
                                "unknown argument: %s", arg.c_str());
            PrintUsage(argv[0]);
            PlatformLogger::Shutdown();
            return 1;
        }
    }

    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        return 1;
    }

    const int fd = ConnectUnixSocket(control_socket);
    if (fd < 0)
    {
        PlatformLogger::Log(LogLevel::kError, "detection_control_client",
                            "connect failed: socket=%s err=%s", control_socket.c_str(),
                            std::strerror(errno));
        PlatformLogger::Shutdown();
        return 1;
    }

    const std::string request_line = request + "\n";
    if (!WriteFull(fd, request_line.data(), request_line.size()))
    {
        PlatformLogger::Log(LogLevel::kError, "detection_control_client",
                            "write failed: %s", std::strerror(errno));
        close(fd);
        PlatformLogger::Shutdown();
        return 1;
    }

    std::string response;
    if (!ReadLine(fd, &response))
    {
        PlatformLogger::Log(LogLevel::kError, "detection_control_client",
                            "read failed: %s", std::strerror(errno));
        close(fd);
        PlatformLogger::Shutdown();
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "detection_control_client",
                        "request=%s", request.c_str());
    PlatformLogger::Log(LogLevel::kInfo, "detection_control_client",
                        "response=%s", response.c_str());

    close(fd);
    PlatformLogger::Shutdown();
    return 0;
}
