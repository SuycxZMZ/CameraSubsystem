/**
 * @file camera_source.h
 * @brief Camera 数据源（V4L2/MMAP 采集实现）
 * @author CameraSubsystem Team
 * @date 2026-01-31
 */

#ifndef CAMERA_SUBSYSTEM_CAMERA_CAMERA_SOURCE_H
#define CAMERA_SUBSYSTEM_CAMERA_CAMERA_SOURCE_H

#include "camera_subsystem/core/buffer_guard.h"
#include "camera_subsystem/core/camera_config.h"
#include "camera_subsystem/core/camera_stream_identity.h"
#include "camera_subsystem/core/frame_descriptor.h"
#include "camera_subsystem/core/frame_handle.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <linux/videodev2.h>

namespace camera_subsystem {
namespace camera {

/**
 * @brief Camera 数据源（V4L2/MMAP 采集实现）
 *
 * 默认路径基于 V4L2 MMAP 采集，并拷贝到 BufferPool 供现有分发链路使用。
 * 当 CameraConfig::io_method_ 显式配置为 IoMethod::kDmaBuf 时，会尝试
 * VIDIOC_EXPBUF 导出 DMA-BUF fd，并通过 FramePacketCallback 交付
 * FrameDescriptor + FrameLease；如果驱动或板端环境不支持导出，则自动回退
 * 到 MMAP + copy 路径。
 */
class CameraSource
{
public:
    using FrameCallback = std::function<void(const core::FrameHandle&)>;
    using FrameCallbackWithBuffer =
        std::function<void(const core::FrameHandle&,
                           const std::shared_ptr<core::BufferGuard>&)>;
    using FramePacketCallback = std::function<void(const core::FramePacket&)>;

    CameraSource();
    ~CameraSource();

    bool Initialize(const core::CameraConfig& config);
    bool Start();
    void Stop();
    bool IsRunning() const;

    void SetDevicePath(const std::string& device_path);
    std::string GetDevicePath() const;
    void SetStreamIdentity(const core::CameraStreamIdentity& identity);
    core::CameraStreamIdentity GetStreamIdentity() const;

    void SetFrameCallback(FrameCallback callback);
    void SetFrameCallbackWithBuffer(FrameCallbackWithBuffer callback);
    void SetFramePacketCallback(FramePacketCallback callback);
    core::CameraConfig GetConfig() const;
    uint64_t GetFrameCount() const;
    uint64_t GetDroppedFrameCount() const;
    bool IsDmaBufPathEnabled() const;
    uint64_t GetDmaBufFrameCount() const;
    uint64_t GetDmaBufExportFailureCount() const;
    uint64_t GetDmaBufLeaseExhaustedCount() const;
    size_t GetDmaBufActiveLeaseCount() const;
    size_t GetDmaBufLeaseInFlightMax() const;
    size_t GetDmaBufMinQueuedCaptureBuffers() const;

private:
    void CaptureLoop();
    void HandleDequeuedBuffer(struct v4l2_buffer& buf, struct v4l2_plane* planes = nullptr);
    void HandleDequeuedBufferCopy(struct v4l2_buffer& buf, struct v4l2_plane* planes = nullptr);
    bool HandleDequeuedBufferDmaBuf(struct v4l2_buffer& buf, struct v4l2_plane* planes = nullptr);
    bool OpenDevice();
    void CloseDevice();
    bool SelectCaptureBufferType(uint32_t capabilities);
    bool ConfigureDevice();
    bool InitMMap();
    bool InitDmaBufExport();
    bool ConfigureMPlaneFormatForProbe();
    bool InitMPlaneBuffersForProbe();
    bool ExportMPlaneDmaBufsForProbe();
    bool InitMPlaneDmaBufExportSkeleton();
    bool InitMPlaneBuffers();
    bool ShouldRunMPlaneProbeOnly() const;
    void CleanupDmaBufExports();
    void CleanupMPlaneProbeBuffers();
    bool ShouldUseDmaBufPath() const;
    void RequeueBuffer(uint32_t buffer_index);
    bool StartStream();
    void StopStream();
    void CleanupBuffers();
    size_t CalculateBufferSize(const core::CameraConfig& config) const;
    void FillFrameLayout(core::FrameHandle& frame, size_t buffer_size) const;
    uint64_t GetTimestampNs() const;
    uint32_t CaptureBufferType() const;

    uint32_t ToV4L2PixelFormat(core::PixelFormat format) const;
    core::PixelFormat FromV4L2PixelFormat(uint32_t format) const;

    core::CameraConfig config_;
    core::CameraStreamIdentity stream_identity_;
    std::string device_path_;
    int device_fd_;
    bool streaming_;
    uint32_t device_capabilities_;
    uint32_t capture_buffer_type_;

    struct Buffer
    {
        void* start = nullptr;
        size_t length = 0;
        int dma_buf_fd = -1;
        bool dma_buf_exported = false;
    };
    std::vector<Buffer> buffers_;

    struct MPlaneProbePlane
    {
        uint32_t bytes_per_line = 0;
        uint32_t length = 0;
        uint32_t bytes_used = 0;
        uint32_t data_offset = 0;
        uint32_t mem_offset = 0;
        int dma_buf_fd = -1;
        bool dma_buf_exported = false;
    };

    struct MPlaneProbeBuffer
    {
        uint32_t index = 0;
        std::vector<MPlaneProbePlane> planes;
    };
    std::vector<MPlaneProbeBuffer> mplane_probe_buffers_;
    uint32_t mplane_probe_plane_count_ = 0;
    uint32_t mplane_probe_width_ = 0;
    uint32_t mplane_probe_height_ = 0;
    uint32_t mplane_probe_fourcc_ = 0;
    std::vector<uint32_t> mplane_probe_strides_;

    struct RequeueContext
    {
        std::mutex mutex;
        int device_fd = -1;
        uint32_t buffer_type = 0;
        uint32_t plane_count = 0;
        uint32_t plane_lengths[8];
        bool active = false;
        std::atomic<size_t> active_leases{0};
    };
    std::shared_ptr<RequeueContext> requeue_context_;

    bool dma_buf_path_enabled_ = false;
    size_t min_queued_capture_buffers_ = 1;
    size_t global_lease_in_flight_max_ = 1;
    std::atomic<uint64_t> dma_buf_frame_count_{0};
    std::atomic<uint64_t> dma_buf_export_failures_{0};
    std::atomic<uint64_t> lease_exhausted_count_{0};

    std::atomic<bool> is_running_;
    std::atomic<uint64_t> frame_count_;
    std::atomic<uint64_t> dropped_frames_;
    std::thread capture_thread_;

    mutable std::mutex callback_mutex_;
    FrameCallback callback_;
    FrameCallbackWithBuffer callback_with_buffer_;
    FramePacketCallback frame_packet_callback_;
    std::atomic<bool> has_frame_packet_callback_{false};

    core::BufferPool buffer_pool_;
    size_t pool_buffer_size_ = 0;
};

} // namespace camera
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CAMERA_CAMERA_SOURCE_H
