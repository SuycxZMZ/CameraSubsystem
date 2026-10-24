/**
 * @file camera_detection_result_client_example.cpp
 * @brief DetectionResult JSON Line 调试客户端
 */

#include "camera_subsystem/platform/platform_logger.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using camera_subsystem::core::LogLevel;
using camera_subsystem::platform::PlatformLogger;

namespace {

std::atomic<bool> g_running{true};

void HandleSignal(int)
{
    g_running.store(false);
}

bool InstallSignalHandlers()
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = HandleSignal;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGINT, &action, nullptr) == 0 &&
           sigaction(SIGTERM, &action, nullptr) == 0;
}

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

bool ExtractStringField(const std::string& line, const std::string& key, std::string* value)
{
    if (!value)
    {
        return false;
    }

    const std::string needle = "\"" + key + "\":\"";
    const size_t start = line.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }

    std::string result;
    bool escape = false;
    for (size_t i = start + needle.size(); i < line.size(); ++i)
    {
        const char ch = line[i];
        if (escape)
        {
            switch (ch)
            {
            case '"':
            case '\\':
            case '/':
                result.push_back(ch);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(ch);
                break;
            }
            escape = false;
            continue;
        }

        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
        {
            *value = result;
            return true;
        }
        result.push_back(ch);
    }

    return false;
}

bool ExtractUint64Field(const std::string& line, const std::string& key, uint64_t* value)
{
    if (!value)
    {
        return false;
    }

    const std::string needle = "\"" + key + "\":";
    const size_t start = line.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }

    const char* begin = line.c_str() + start + needle.size();
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(begin, &end, 10);
    if (begin == end || errno != 0)
    {
        return false;
    }
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool ExtractDoubleField(const std::string& line, const std::string& key, double* value)
{
    if (!value)
    {
        return false;
    }

    const std::string needle = "\"" + key + "\":";
    const size_t start = line.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }

    const char* begin = line.c_str() + start + needle.size();
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(begin, &end);
    if (begin == end || errno != 0)
    {
        return false;
    }
    *value = parsed;
    return true;
}

uint32_t CountObjects(const std::string& line)
{
    const std::string needle = "\"class_id\":";
    uint32_t count = 0;
    size_t pos = 0;
    while (true)
    {
        pos = line.find(needle, pos);
        if (pos == std::string::npos)
        {
            break;
        }
        ++count;
        pos += needle.size();
    }
    return count;
}

struct ClientStats
{
    uint64_t frames = 0;
    uint64_t bytes = 0;
    uint64_t parse_failures = 0;
    uint64_t total_objects = 0;
    uint64_t last_frame_id = 0;
    std::string last_stream_id;
    double last_inference_ms = 0.0;
    double last_total_ms = 0.0;
    uint32_t last_object_count = 0;
};

void PrintUsage(const char* program)
{
    PlatformLogger::Log(LogLevel::kInfo, "detection_result_client",
                        "usage: %s [--result-socket path] [--raw]", program);
}

} // namespace

int main(int argc, char* argv[])
{
    std::string result_socket = "/tmp/camera_subsystem_detection_result.sock";
    bool raw_output = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--result-socket" && i + 1 < argc)
        {
            result_socket = argv[++i];
        }
        else if (arg == "--raw")
        {
            raw_output = true;
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
            PlatformLogger::Log(LogLevel::kError, "detection_result_client",
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

    if (!InstallSignalHandlers())
    {
        PlatformLogger::Log(LogLevel::kError, "detection_result_client",
                            "failed to install signal handlers");
        PlatformLogger::Shutdown();
        return 1;
    }

    const int fd = ConnectUnixSocket(result_socket);
    if (fd < 0)
    {
        PlatformLogger::Log(LogLevel::kError, "detection_result_client",
                            "connect failed: socket=%s err=%s", result_socket.c_str(),
                            std::strerror(errno));
        PlatformLogger::Shutdown();
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "detection_result_client",
                        "connected: result_socket=%s raw=%u", result_socket.c_str(),
                        raw_output ? 1U : 0U);
    PlatformLogger::Log(LogLevel::kInfo, "detection_result_client",
                        "sec | frames | fps | bytes | parse_fail | objects | last_stream | "
                        "last_frame | infer_ms | total_ms");

    ClientStats stats;
    std::string read_buffer;
    read_buffer.reserve(64 * 1024);
    char io_buffer[16 * 1024];

    auto next_report = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    uint64_t last_frames = 0;
    uint64_t elapsed_sec = 0;

    while (g_running.load())
    {
        const ssize_t ret = read(fd, io_buffer, sizeof(io_buffer));
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            PlatformLogger::Log(LogLevel::kError, "detection_result_client",
                                "read failed: %s", std::strerror(errno));
            break;
        }
        if (ret == 0)
        {
            PlatformLogger::Log(LogLevel::kInfo, "detection_result_client",
                                "server disconnected");
            break;
        }

        stats.bytes += static_cast<uint64_t>(ret);
        read_buffer.append(io_buffer, static_cast<size_t>(ret));

        size_t newline = 0;
        while ((newline = read_buffer.find('\n')) != std::string::npos)
        {
            std::string line = read_buffer.substr(0, newline);
            read_buffer.erase(0, newline + 1);

            if (line.empty())
            {
                continue;
            }

            ++stats.frames;
            if (raw_output)
            {
                PlatformLogger::Log(LogLevel::kInfo, "detection_result_client",
                                    "raw=%s", line.c_str());
            }

            if (!ExtractStringField(line, "stream_id", &stats.last_stream_id) ||
                !ExtractUint64Field(line, "frame_id", &stats.last_frame_id) ||
                !ExtractDoubleField(line, "inference_ms", &stats.last_inference_ms) ||
                !ExtractDoubleField(line, "total_ms", &stats.last_total_ms))
            {
                ++stats.parse_failures;
                continue;
            }

            stats.last_object_count = CountObjects(line);
            stats.total_objects += stats.last_object_count;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_report)
        {
            ++elapsed_sec;
            const uint64_t fps = stats.frames - last_frames;
            last_frames = stats.frames;
            PlatformLogger::Log(
                LogLevel::kInfo, "detection_result_client",
                "sec=%lu | frames=%lu | fps=%lu | bytes=%lu | parse_fail=%lu | objects=%lu | "
                "last_stream=%s | last_frame=%lu | infer_ms=%.2f | total_ms=%.2f",
                elapsed_sec, stats.frames, fps, stats.bytes, stats.parse_failures,
                stats.total_objects, stats.last_stream_id.c_str(), stats.last_frame_id,
                stats.last_inference_ms, stats.last_total_ms);
            next_report = now + std::chrono::seconds(1);
        }
    }

    close(fd);
    PlatformLogger::Shutdown();
    return 0;
}
