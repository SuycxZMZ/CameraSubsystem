/**
 * @file camera_subscriber_example.cpp
 * @brief 订阅端示例程序（独立进程）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 *
 * 用法：
 *   ./camera_subscriber_example [output_dir] [control_socket] [data_socket] [device_path]
 *
 * 默认参数：
 * 1. output_dir    : ./subscriber_frames
 * 2. control_socket: /tmp/camera_subsystem_control.sock
 * 3. data_socket   : /tmp/camera_subsystem_data.sock
 * 4. device_path   : CAMERA_SUBSYSTEM_DEFAULT_CAMERA（通常为 /dev/video0）
 *
 * 运行流程：
 * 1. 连接数据面 socket，接收核心发布端发送的帧头+帧数据。
 * 2. 通过控制面发送 Subscribe 请求（默认 CAMERA_SUBSYSTEM_DEFAULT_CAMERA）。
 * 3. 循环接收帧并 sleep 5ms 模拟上层业务处理耗时。
 * 4. 每秒打印一次统计信息，并保存一张图片到 output_dir。
 * 5. 图片最多保留 10 张，文件名按槽位 0~9 循环覆盖。
 * 6. 默认无限运行，收到 Ctrl+C（SIGINT/SIGTERM）后发送 Unsubscribe 并退出。
 *
 * 输出说明：
 * - 每秒打印：sec | frames | fps | received_bytes | save_fail | image
 */

#include "camera_subsystem/core/types.h"
#include "camera_subsystem/ipc/camera_channel_contract.h"
#include "camera_subsystem/ipc/camera_control_client.h"
#include "camera_subsystem/ipc/camera_data_ipc.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{

using camera_subsystem::core::LogLevel;
using camera_subsystem::core::PixelFormat;
using camera_subsystem::ipc::CameraClientRole;
using camera_subsystem::ipc::CameraControlClient;
using camera_subsystem::ipc::CameraControlResponse;
using camera_subsystem::ipc::CameraDataFrameHeader;
using camera_subsystem::ipc::CameraEndpoint;
using camera_subsystem::ipc::CameraControlStatus;
using camera_subsystem::platform::PlatformLogger;

std::atomic<bool> g_running(true);

void SignalHandler(int signo)
{
    (void)signo;
    g_running.store(false);
}

bool ReadFull(int fd, void* buffer, size_t length)
{
    size_t total = 0;
    auto* data = reinterpret_cast<uint8_t*>(buffer);

    while (total < length)
    {
        const ssize_t n = read(fd, data + total, length - total);
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

int ConnectDataSocket(const std::string& socket_path)
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

struct FrameSnapshot
{
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::kUnknown;
};

bool SaveSnapshot(const FrameSnapshot& snapshot,
                  const std::filesystem::path& output_dir,
                  uint64_t slot,
                  std::string* saved_path,
                  std::string* error_message)
{
    if (!saved_path || !error_message)
    {
        return false;
    }

    *saved_path = std::string();
    *error_message = std::string();

    if (snapshot.data.empty() || snapshot.width == 0 || snapshot.height == 0)
    {
        *error_message = "invalid snapshot";
        return false;
    }

    std::filesystem::path file_path;
    std::ofstream out;

    if (snapshot.format == PixelFormat::kMJPEG)
    {
        file_path = output_dir / ("frame_" + std::to_string(slot) + ".jpg");
        out.open(file_path, std::ios::binary);
        if (!out.is_open())
        {
            *error_message = std::string("open failed: ") + std::strerror(errno);
            return false;
        }
        out.write(reinterpret_cast<const char*>(snapshot.data.data()),
                  static_cast<std::streamsize>(snapshot.data.size()));
    }
    else if (snapshot.format == PixelFormat::kRGB888 ||
             snapshot.format == PixelFormat::kRGBA8888)
    {
        file_path = output_dir / ("frame_" + std::to_string(slot) + ".ppm");
        out.open(file_path, std::ios::binary);
        if (!out.is_open())
        {
            *error_message = std::string("open failed: ") + std::strerror(errno);
            return false;
        }

        out << "P6\n" << snapshot.width << " " << snapshot.height << "\n255\n";
        if (snapshot.format == PixelFormat::kRGB888)
        {
            out.write(reinterpret_cast<const char*>(snapshot.data.data()),
                      static_cast<std::streamsize>(snapshot.data.size()));
        }
        else
        {
            for (size_t i = 0; i + 3 < snapshot.data.size(); i += 4)
            {
                out.write(reinterpret_cast<const char*>(&snapshot.data[i]), 3);
            }
        }
    }
    else
    {
        // 非 RGB/MJPEG 时保存灰度 PGM（优先取 Y 平面/前 width*height 字节）
        file_path = output_dir / ("frame_" + std::to_string(slot) + ".pgm");
        out.open(file_path, std::ios::binary);
        if (!out.is_open())
        {
            *error_message = std::string("open failed: ") + std::strerror(errno);
            return false;
        }

        out << "P5\n" << snapshot.width << " " << snapshot.height << "\n255\n";
        const size_t luma_size = static_cast<size_t>(snapshot.width) * snapshot.height;
        const size_t write_size = std::min(luma_size, snapshot.data.size());
        out.write(reinterpret_cast<const char*>(snapshot.data.data()),
                  static_cast<std::streamsize>(write_size));
    }

    if (!out.good())
    {
        *error_message = std::string("write failed: ") + std::strerror(errno);
        return false;
    }

    *saved_path = file_path.string();
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::string output_dir = "./subscriber_frames";
    std::string control_socket_path = camera_subsystem::ipc::kDefaultCameraControlSocketPath;
    std::string data_socket_path = camera_subsystem::ipc::kDefaultCameraDataSocketPath;
    std::string device_path = CAMERA_SUBSYSTEM_DEFAULT_CAMERA;

    if (argc > 1)
    {
        output_dir = argv[1];
    }
    if (argc > 2)
    {
        control_socket_path = argv[2];
    }
    if (argc > 3)
    {
        data_socket_path = argv[3];
    }
    if (argc > 4)
    {
        device_path = argv[4];
    }

    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        return 1;
    }

    std::filesystem::create_directories(output_dir);
    const std::filesystem::path output_dir_path = std::filesystem::absolute(output_dir);

    // 先连接数据面，确保订阅建立后可立即收帧
    int data_fd = -1;
    for (int retry = 0; retry < 50 && g_running.load(); ++retry)
    {
        data_fd = ConnectDataSocket(data_socket_path);
        if (data_fd >= 0)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (data_fd < 0)
    {
        PlatformLogger::Log(LogLevel::kError, "subscriber",
                            "connect data socket failed: %s", data_socket_path.c_str());
        PlatformLogger::Shutdown();
        return 1;
    }

    CameraControlClient control_client;
    if (!control_client.Connect(control_socket_path))
    {
        PlatformLogger::Log(LogLevel::kError, "subscriber",
                            "connect control socket failed: %s",
                            control_socket_path.c_str());
        close(data_fd);
        PlatformLogger::Shutdown();
        return 1;
    }

    const std::string client_id = "camera_subscriber_" + std::to_string(getpid());
    const CameraEndpoint endpoint =
        camera_subsystem::ipc::MakeCameraEndpoint(0,
                                                  camera_subsystem::ipc::CameraBusType::kDefault,
                                                  0,
                                                  device_path.c_str());

    CameraControlResponse response;
    if (!control_client.Subscribe(client_id, CameraClientRole::kSubscriber, endpoint, &response))
    {
        PlatformLogger::Log(LogLevel::kError, "subscriber",
                            "subscribe failed: status=%u msg=%s",
                            static_cast<uint32_t>(response.status), response.message);
        control_client.Disconnect();
        close(data_fd);
        PlatformLogger::Shutdown();
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "subscriber",
                        "subscriber started, client_id=%s, output_dir=%s, device=%s",
                        client_id.c_str(), output_dir_path.c_str(), endpoint.device_path);
    PlatformLogger::Log(LogLevel::kInfo, "subscriber",
                        "sec | frames | fps | received_bytes | save_fail | image");

    std::mutex snapshot_mutex;
    FrameSnapshot latest_snapshot;

    uint64_t total_frames = 0;
    uint64_t total_bytes = 0;
    uint64_t save_fail_count = 0;
    uint64_t elapsed_sec = 0;
    uint64_t last_frames = 0;

    auto next_report_time = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    while (g_running.load())
    {
        pollfd pfd;
        pfd.fd = data_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        const int poll_ret = poll(&pfd, 1, 200);
        if (poll_ret > 0 && (pfd.revents & POLLIN))
        {
            CameraDataFrameHeader header;
            if (!ReadFull(data_fd, &header, sizeof(header)))
            {
                PlatformLogger::Log(LogLevel::kWarning, "subscriber",
                                    "data channel closed while reading header");
                break;
            }

            if (!camera_subsystem::ipc::IsCameraDataFrameHeaderValid(header))
            {
                PlatformLogger::Log(LogLevel::kWarning, "subscriber",
                                    "invalid frame header: magic=0x%x version=%u frame_size=%u",
                                    header.magic, header.version, header.frame_size);
                break;
            }

            if (header.frame_size > 64U * 1024U * 1024U)
            {
                PlatformLogger::Log(LogLevel::kWarning, "subscriber",
                                    "frame too large: %u", header.frame_size);
                break;
            }

            std::vector<uint8_t> frame_buffer(header.frame_size);
            if (!ReadFull(data_fd, frame_buffer.data(), frame_buffer.size()))
            {
                PlatformLogger::Log(LogLevel::kWarning, "subscriber",
                                    "data channel closed while reading frame body");
                break;
            }

            {
                std::lock_guard<std::mutex> lock(snapshot_mutex);
                latest_snapshot.data = std::move(frame_buffer);
                latest_snapshot.width = header.width;
                latest_snapshot.height = header.height;
                latest_snapshot.format = static_cast<PixelFormat>(header.pixel_format);
            }

            ++total_frames;
            total_bytes += header.frame_size;

            // 模拟上层处理
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        else if (poll_ret < 0 && errno != EINTR)
        {
            PlatformLogger::Log(LogLevel::kError, "subscriber",
                                "poll failed: %s", strerror(errno));
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_report_time)
        {
            ++elapsed_sec;
            const uint64_t fps = total_frames - last_frames;
            last_frames = total_frames;

            FrameSnapshot snapshot_copy;
            {
                std::lock_guard<std::mutex> lock(snapshot_mutex);
                snapshot_copy = latest_snapshot;
            }

            uint64_t image_slot = 0;
            std::string image_info = "none";
            if (elapsed_sec > 0)
            {
                image_slot = (elapsed_sec - 1) % 10;
                std::string saved_path;
                std::string save_error;
                if (snapshot_copy.data.empty() ||
                    !SaveSnapshot(snapshot_copy, output_dir_path, image_slot, &saved_path, &save_error))
                {
                    ++save_fail_count;
                    image_info = "save_failed";
                    if (!save_error.empty())
                    {
                        PlatformLogger::Log(
                            LogLevel::kWarning, "subscriber",
                            "save image failed: slot=%" PRIu64 " reason=%s",
                            image_slot, save_error.c_str());
                    }
                }
                else
                {
                    image_info = "img_slot=" + std::to_string(image_slot);
                }
            }

            PlatformLogger::Log(LogLevel::kInfo, "subscriber",
                                "sec=%" PRIu64 " | frames=%" PRIu64 " | fps=%" PRIu64
                                " | received_bytes=%" PRIu64 " | save_fail=%" PRIu64 " | %s",
                                elapsed_sec, total_frames, fps, total_bytes, save_fail_count,
                                image_info.c_str());

            next_report_time += std::chrono::seconds(1);
        }
    }

    (void)control_client.Unsubscribe(client_id, CameraClientRole::kSubscriber, endpoint, &response);
    control_client.Disconnect();
    close(data_fd);

    PlatformLogger::Log(LogLevel::kInfo, "subscriber",
                        "summary: frames=%" PRIu64 " received_bytes=%" PRIu64
                        " save_fail=%" PRIu64,
                        total_frames, total_bytes, save_fail_count);
    PlatformLogger::Shutdown();
    return 0;
}
