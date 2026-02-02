/**
 * @file camera_source_stress_test.cpp
 * @brief CameraSource 压力测试程序
 * @author CameraSubsystem Team
 * @date 2026-01-31
 */

#include "camera_subsystem/broker/frame_broker.h"
#include "camera_subsystem/camera/camera_source.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <atomic>
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
                  const std::shared_ptr<core::BufferBlock>& buffer_ref)
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

    const std::string output_dir = "stress_frames";
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);

    auto start_time = std::chrono::steady_clock::now();
    auto last_report = start_time;
    uint64_t last_frame_count = 0;
    uint64_t image_index = 0;

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
                std::string file_path;

                if (snapshot.format == core::PixelFormat::kMJPEG)
                {
                    file_path = output_dir + "/frame_" + std::to_string(slot) + ".jpg";
                    std::ofstream out(file_path, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(snapshot.data.data()),
                              static_cast<std::streamsize>(snapshot.data.size()));
                }
                else if (snapshot.format == core::PixelFormat::kRGB888 ||
                         snapshot.format == core::PixelFormat::kRGBA8888)
                {
                    file_path = output_dir + "/frame_" + std::to_string(slot) + ".ppm";
                    std::ofstream out(file_path, std::ios::binary);
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
                    file_path = output_dir + "/frame_" + std::to_string(slot) + ".pgm";
                    std::ofstream out(file_path, std::ios::binary);
                    out << "P5\n" << snapshot.width << " " << snapshot.height << "\n255\n";
                    const size_t luma_size =
                        static_cast<size_t>(snapshot.width) * snapshot.height;
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
                    // NV12/H264/H265 统一保存为灰度或原始数据
                    file_path = output_dir + "/frame_" + std::to_string(slot) + ".pgm";
                    std::ofstream out(file_path, std::ios::binary);
                    out << "P5\n" << snapshot.width << " " << snapshot.height << "\n255\n";
                    const size_t luma_size =
                        static_cast<size_t>(snapshot.width) * snapshot.height;
                    const size_t write_size = std::min(luma_size, snapshot.data.size());
                    out.write(reinterpret_cast<const char*>(snapshot.data.data()),
                              static_cast<std::streamsize>(write_size));
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
                                  "Summary: frames=%lu received=%lu",
                                  camera_source.GetFrameCount(),
                                  total_received);

    platform::PlatformLogger::Shutdown();
    return 0;
}
