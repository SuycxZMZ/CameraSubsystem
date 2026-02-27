/**
 * @file camera_source_stress_test.cpp
 * @brief CameraSource 压力测试程序
 * @author CameraSubsystem Team
 * @date 2026-01-31
 * 
 * @usage
 * 用法: ./camera_source_stress_test [duration_seconds] [device_path] [output_dir]
 * 
 * 参数:
 *   duration_seconds: 测试持续时间（秒），默认 20 秒
 *   device_path: Camera 设备路径，默认 /dev/video0
 *   output_dir: 图片输出目录，默认 ./stress_frames
 * 
 * 示例:
 *   # 默认测试（20 秒，/dev/video0）
 *   sudo ./camera_source_stress_test
 * 
 *   # 自定义测试（30 秒，/dev/video1）
 *   sudo ./camera_source_stress_test 30 /dev/video1
 *
 *   # 自定义输出目录
 *   sudo ./camera_source_stress_test 20 /dev/video0 ./stress_frames
 * 
 * 输出:
 *   - 每秒输出一行统计日志：sec | frames | fps | dispatched | dropped | queue | image
 *   - 每秒保存一张图片到 stress_frames/ 目录（最多保留 10 张，循环覆盖）
 *   - 图片格式根据 Camera 输出格式自动选择：
 *     * MJPEG → .jpg
 *     * RGB888/RGBA8888 → .ppm
 *     * YUYV/NV12/H264/H265 → .pgm（灰度或原始 Y 平面）
 * 
 * 注意:
 *   - 需要 root 权限或 video 组权限才能访问 Camera 设备
 *   - 程序会打印图片输出目录绝对路径和保存失败原因
 *   - 使用 v4l2-ctl --list-formats-ext 查看设备支持的格式
 */

#include "camera_subsystem/broker/frame_broker.h"
#include "camera_subsystem/camera/camera_source.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace camera_subsystem;

std::atomic<bool> g_running_(true);

struct FrameSnapshot
{
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    core::PixelFormat format = core::PixelFormat::kUnknown;
};

std::mutex g_frame_mutex_;
FrameSnapshot g_latest_frame_;

bool IsDirectoryWritable(const std::filesystem::path& dir_path, std::string* error_message)
{
    if (!error_message)
    {
        return false;
    }

    *error_message = std::string();

    if (!std::filesystem::exists(dir_path))
    {
        *error_message = "directory does not exist";
        return false;
    }

    if (!std::filesystem::is_directory(dir_path))
    {
        *error_message = "path is not a directory";
        return false;
    }

    errno = 0;
    if (access(dir_path.c_str(), W_OK) != 0)
    {
        *error_message = std::string("no write permission: ") + std::strerror(errno);
        return false;
    }

    return true;
}

bool SaveSnapshotToFile(const FrameSnapshot& snapshot,
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

    if (snapshot.data.empty())
    {
        *error_message = "snapshot data is empty";
        return false;
    }

    if (snapshot.width == 0 || snapshot.height == 0)
    {
        *error_message = "invalid frame size";
        return false;
    }

    std::filesystem::path file_path;
    std::ofstream out;

    if (snapshot.format == core::PixelFormat::kMJPEG)
    {
        file_path = output_dir / ("frame_" + std::to_string(slot) + ".jpg");
        errno = 0;
        out.open(file_path, std::ios::binary);
        if (!out.is_open())
        {
            *error_message = std::string("failed to open file: ") + std::strerror(errno);
            return false;
        }
        out.write(reinterpret_cast<const char*>(snapshot.data.data()),
                  static_cast<std::streamsize>(snapshot.data.size()));
    }
    else if (snapshot.format == core::PixelFormat::kRGB888 ||
             snapshot.format == core::PixelFormat::kRGBA8888)
    {
        file_path = output_dir / ("frame_" + std::to_string(slot) + ".ppm");
        errno = 0;
        out.open(file_path, std::ios::binary);
        if (!out.is_open())
        {
            *error_message = std::string("failed to open file: ") + std::strerror(errno);
            return false;
        }
        out << "P6\n" << snapshot.width << " " << snapshot.height << "\n255\n";
        if (snapshot.format == core::PixelFormat::kRGB888)
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
    else if (snapshot.format == core::PixelFormat::kYUYV)
    {
        file_path = output_dir / ("frame_" + std::to_string(slot) + ".pgm");
        errno = 0;
        out.open(file_path, std::ios::binary);
        if (!out.is_open())
        {
            *error_message = std::string("failed to open file: ") + std::strerror(errno);
            return false;
        }
        out << "P5\n" << snapshot.width << " " << snapshot.height << "\n255\n";

        const size_t luma_size = static_cast<size_t>(snapshot.width) * snapshot.height;
        std::vector<uint8_t> luma;
        luma.reserve(luma_size);
        for (size_t i = 0; i < luma_size; ++i)
        {
            const size_t src = i * 2;
            if (src < snapshot.data.size())
            {
                luma.push_back(snapshot.data[src]);
            }
            else
            {
                luma.push_back(0);
            }
        }

        out.write(reinterpret_cast<const char*>(luma.data()),
                  static_cast<std::streamsize>(luma.size()));
    }
    else
    {
        file_path = output_dir / ("frame_" + std::to_string(slot) + ".pgm");
        errno = 0;
        out.open(file_path, std::ios::binary);
        if (!out.is_open())
        {
            *error_message = std::string("failed to open file: ") + std::strerror(errno);
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

void SignalHandler(int signal)
{
    (void)signal;
    g_running_.store(false);
}

class CounterSubscriber : public broker::IFrameSubscriber
{
public:
    explicit CounterSubscriber(std::string name, uint8_t priority)
        : name_(std::move(name))
        , priority_(priority)
        , received_count_(0)
    {
    }

    void OnFrame(const core::FrameHandle& /*frame*/) override
    {
        received_count_.fetch_add(1);
    }

    const char* GetSubscriberName() const override
    {
        return name_.c_str();
    }

    uint8_t GetPriority() const override
    {
        return priority_;
    }

    uint64_t GetReceivedCount() const
    {
        return received_count_.load();
    }

private:
    std::string name_;
    uint8_t priority_;
    std::atomic<uint64_t> received_count_;
};

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    if (!platform::PlatformLogger::Initialize(std::string(), core::LogLevel::kInfo))
    {
        return 1;
    }

    int duration_seconds = 20;
    if (argc > 1)
    {
        duration_seconds = std::max(1, std::atoi(argv[1]));
    }

    std::string device_path = "/dev/video0";
    if (argc > 2)
    {
        device_path = argv[2];
    }

    std::filesystem::path output_dir = "stress_frames";
    if (argc > 3)
    {
        output_dir = argv[3];
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_stress",
                                  "CameraSource stress test start, duration=%ds, device=%s",
                                  duration_seconds, device_path.c_str());

    broker::FrameBroker broker;
    broker.Start(4);

    const int kSubscriberCount = 4;
    std::vector<std::shared_ptr<CounterSubscriber>> subscribers;
    subscribers.reserve(kSubscriberCount);
    for (int i = 0; i < kSubscriberCount; ++i)
    {
        auto subscriber = std::make_shared<CounterSubscriber>(
            "subscriber_" + std::to_string(i),
            static_cast<uint8_t>(100 + i)
        );
        broker.Subscribe(subscriber);
        subscribers.push_back(subscriber);
    }

    camera::CameraSource camera_source;
    camera_source.SetDevicePath(device_path);
    auto config = core::CameraConfig::GetDefault();
    config.fps_ = 30;
    config.buffer_count_ = 4;

    if (!camera_source.Initialize(config))
    {
        platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_stress",
                                      "Failed to initialize CameraSource. "
                                      "Check device permissions and path.");
        platform::PlatformLogger::Shutdown();
        return 0;
    }

    camera_source.SetFrameCallbackWithBuffer(
        [&broker](const core::FrameHandle& frame,
                  const std::shared_ptr<core::BufferGuard>& buffer_ref)
        {
            broker.PublishFrame(frame, buffer_ref);

            std::lock_guard<std::mutex> lock(g_frame_mutex_);
            g_latest_frame_.width = frame.width_;
            g_latest_frame_.height = frame.height_;
            g_latest_frame_.format = frame.format_;
            g_latest_frame_.data.resize(frame.buffer_size_);
            if (frame.virtual_address_ != nullptr && frame.buffer_size_ > 0)
            {
                std::memcpy(g_latest_frame_.data.data(),
                            frame.virtual_address_,
                            frame.buffer_size_);
            }
        });

    if (!camera_source.Start())
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_stress",
                                      "Failed to start CameraSource");
        return 1;
    }

    std::error_code ec;
    std::filesystem::path output_dir_abs = std::filesystem::absolute(output_dir);
    std::filesystem::create_directories(output_dir_abs, ec);
    if (ec)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_stress",
                                      "Failed to create output dir: %s, error=%s",
                                      output_dir_abs.c_str(),
                                      ec.message().c_str());
        camera_source.Stop();
        broker.Stop();
        platform::PlatformLogger::Shutdown();
        return 1;
    }

    std::string writable_error;
    if (!IsDirectoryWritable(output_dir_abs, &writable_error))
    {
        std::filesystem::path fallback_dir =
            std::filesystem::temp_directory_path() / "camera_subsystem_stress_frames";
        std::error_code fallback_ec;
        std::filesystem::create_directories(fallback_dir, fallback_ec);

        std::string fallback_error;
        if (!fallback_ec && IsDirectoryWritable(fallback_dir, &fallback_error))
        {
            platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_stress",
                                          "Output dir not writable: %s, reason=%s. "
                                          "Fallback to: %s",
                                          output_dir_abs.c_str(),
                                          writable_error.c_str(),
                                          fallback_dir.c_str());
            output_dir_abs = fallback_dir;
        }
        else
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_stress",
                                          "Output dir not writable: %s, reason=%s. "
                                          "Fallback dir unavailable: %s, reason=%s",
                                          output_dir_abs.c_str(),
                                          writable_error.c_str(),
                                          fallback_dir.c_str(),
                                          fallback_error.c_str());
            camera_source.Stop();
            broker.Stop();
            platform::PlatformLogger::Shutdown();
            return 1;
        }
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_stress",
                                  "Image output directory: %s",
                                  output_dir_abs.c_str());

    auto start_time = std::chrono::steady_clock::now();
    auto last_report = start_time;
    uint64_t last_frame_count = 0;
    uint64_t image_index = 0;
    uint64_t saved_image_count = 0;
    uint64_t save_fail_count = 0;

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_stress",
                                  "sec | frames | fps | dispatched | dropped | queue | image");

    while (g_running_.load())
    {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= duration_seconds)
        {
            break;
        }

        auto since_report = std::chrono::steady_clock::now() - last_report;
        if (std::chrono::duration_cast<std::chrono::seconds>(since_report).count() >= 1)
        {
            const uint64_t total_frames = camera_source.GetFrameCount();
            const uint64_t fps = total_frames - last_frame_count;
            last_frame_count = total_frames;

            const auto stats = broker.GetStats();
            platform::PlatformLogger::Log(
                core::LogLevel::kInfo,
                "camera_stress",
                "sec=%ld | frames=%lu | fps=%lu | dispatched=%lu | dropped=%lu | queue=%zu | %s%lu",
                static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()),
                total_frames,
                fps,
                stats.dispatched_tasks,
                stats.dropped_tasks,
                stats.queue_size,
                "img_slot=",
                (image_index % 10)
            );

            // 每秒保存一张图片（最多保留10张，循环覆盖）
            FrameSnapshot snapshot;
            {
                std::lock_guard<std::mutex> lock(g_frame_mutex_);
                snapshot = g_latest_frame_;
            }

            if (!snapshot.data.empty())
            {
                const uint64_t slot = image_index % 10;
                std::string saved_path;
                std::string error_message;
                if (SaveSnapshotToFile(snapshot,
                                       output_dir_abs,
                                       slot,
                                       &saved_path,
                                       &error_message))
                {
                    saved_image_count++;
                }
                else
                {
                    save_fail_count++;
                    platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_stress",
                                                  "Failed to save frame slot=%lu, format=%s, "
                                                  "reason=%s, output_dir=%s",
                                                  slot,
                                                  core::PixelFormatToString(snapshot.format),
                                                  error_message.c_str(),
                                                  output_dir_abs.c_str());
                }
            }
            else
            {
                platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_stress",
                                              "No frame available to save");
            }

            image_index++;
            last_report = std::chrono::steady_clock::now();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    camera_source.Stop();
    broker.Stop();

    uint64_t total_received = 0;
    for (const auto& subscriber : subscribers)
    {
        total_received += subscriber->GetReceivedCount();
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_stress",
                                  "Summary: frames=%lu received=%lu saved=%lu save_failed=%lu dir=%s",
                                  camera_source.GetFrameCount(),
                                  total_received,
                                  saved_image_count,
                                  save_fail_count,
                                  output_dir_abs.c_str());

    platform::PlatformLogger::Shutdown();
    return 0;
}
