/**
 * @file camera_source.h
 * @brief Camera 数据源（模拟实现）
 * @author CameraSubsystem Team
 * @date 2026-01-31
 */

#ifndef CAMERA_SUBSYSTEM_CAMERA_CAMERA_SOURCE_H
#define CAMERA_SUBSYSTEM_CAMERA_CAMERA_SOURCE_H

#include "camera_subsystem/core/camera_config.h"
#include "camera_subsystem/core/frame_handle.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camera_subsystem {
namespace camera {

/**
 * @brief Camera 数据源（模拟实现）
 *
 * 当前版本基于 V4L2 进行采集，用于 Ubuntu 上调试，
 * 后续可扩展为 RK3576 交叉编译环境。
 */
class CameraSource
{
public:
    using FrameCallback = std::function<void(const core::FrameHandle&)>;

    CameraSource();
    ~CameraSource();

    bool Initialize(const core::CameraConfig& config);
    bool Start();
    void Stop();
    bool IsRunning() const;

    void SetDevicePath(const std::string& device_path);
    std::string GetDevicePath() const;

    void SetFrameCallback(FrameCallback callback);
    core::CameraConfig GetConfig() const;
    uint64_t GetFrameCount() const;

private:
    void CaptureLoop();
    bool OpenDevice();
    void CloseDevice();
    bool ConfigureDevice();
    bool InitMMap();
    bool StartStream();
    void StopStream();
    void CleanupBuffers();
    size_t CalculateBufferSize(const core::CameraConfig& config) const;
    void FillFrameLayout(core::FrameHandle& frame, size_t buffer_size) const;
    uint64_t GetTimestampNs() const;

    uint32_t ToV4L2PixelFormat(core::PixelFormat format) const;

    core::CameraConfig config_;
    std::string device_path_;
    int device_fd_;
    bool streaming_;

    struct Buffer
    {
        void* start = nullptr;
        size_t length = 0;
    };
    std::vector<Buffer> buffers_;

    std::atomic<bool> is_running_;
    std::atomic<uint64_t> frame_count_;
    std::thread capture_thread_;

    mutable std::mutex callback_mutex_;
    FrameCallback callback_;
};

} // namespace camera
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CAMERA_CAMERA_SOURCE_H
