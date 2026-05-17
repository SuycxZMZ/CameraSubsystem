#include "camera_subsystem/camera/camera_source.h"

#include "camera_subsystem/core/frame_lease.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <memory>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <thread>
#include <unistd.h>

namespace camera_subsystem
{
namespace camera
{

namespace
{

int Xioctl(int fd, int request, void* arg)
{
    int ret = 0;
    do
    {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

uint32_t EffectiveCapabilities(const v4l2_capability& cap)
{
    if ((cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0)
    {
        return cap.device_caps;
    }
    return cap.capabilities;
}

void BuildStreamParm(struct v4l2_streamparm* parm, uint32_t buffer_type, uint32_t fps)
{
    memset(parm, 0, sizeof(*parm));
    parm->type = static_cast<v4l2_buf_type>(buffer_type);
    parm->parm.capture.timeperframe.numerator = 1;
    parm->parm.capture.timeperframe.denominator = std::max<uint32_t>(1, fps);
}

uint32_t ExtractFpsFromParm(const struct v4l2_streamparm& parm)
{
    return parm.parm.capture.timeperframe.denominator /
           std::max<uint32_t>(1, parm.parm.capture.timeperframe.numerator);
}

} // namespace

CameraSource::CameraSource()
    : config_(), stream_identity_(core::MakeDefaultCameraStreamIdentity()),
      device_path_("/dev/video0"), device_fd_(-1), streaming_(false), device_capabilities_(0),
      capture_buffer_type_(V4L2_BUF_TYPE_VIDEO_CAPTURE),
      requeue_context_(std::make_shared<RequeueContext>()), is_running_(false), frame_count_(0),
      dropped_frames_(0)
{
}

CameraSource::~CameraSource()
{
    Stop();
    CleanupDmaBufExports();
    CleanupMPlaneProbeBuffers();
    CleanupBuffers();
    CloseDevice();
}

bool CameraSource::Initialize(const core::CameraConfig& config)
{
    Stop();
    CleanupDmaBufExports();
    CleanupMPlaneProbeBuffers();
    CleanupBuffers();
    CloseDevice();

    if (!config.IsValid())
    {
        return false;
    }

    config_ = config;
    is_degraded_.store(false);
    current_target_fps_.store(config_.fps_);
    degradation_count_.store(0);
    degradation_recovery_count_.store(0);
    degradation_failure_count_.store(0);
    window_start_ns_ = 0;
    window_frame_count_ = 0;

    if (!OpenDevice())
    {
        return false;
    }

    if (ShouldRunMPlaneProbeOnly())
    {
        const bool ok = InitMPlaneDmaBufExportSkeleton();
        CleanupMPlaneProbeBuffers();
        CloseDevice();
        state_ = SourceState::kIdle;
        platform::PlatformLogger::Log(ok ? core::LogLevel::kInfo : core::LogLevel::kError,
                                      "camera_source", "MPLANE probe-only initialize result=%s",
                                      ok ? "PASS" : "FAIL");
        return ok;
    }

    if (!SelectCaptureBufferType(device_capabilities_))
    {
        CloseDevice();
        return false;
    }

    if (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
    {
        if (!InitMPlaneBuffers())
        {
            CleanupMPlaneProbeBuffers();
            CloseDevice();
            return false;
        }
    }
    else
    {
        if (!ConfigureDevice())
        {
            CloseDevice();
            return false;
        }

        if (!InitMMap())
        {
            CleanupDmaBufExports();
            CleanupBuffers();
            CloseDevice();
            return false;
        }

        pool_buffer_size_ = 0;
        if (!buffers_.empty())
        {
            pool_buffer_size_ = buffers_[0].length;
        }
        if (pool_buffer_size_ == 0)
        {
            pool_buffer_size_ = CalculateBufferSize(config_);
        }

        if (!buffer_pool_.Initialize(config_.buffer_count_, pool_buffer_size_))
        {
            CleanupDmaBufExports();
            CleanupBuffers();
            CloseDevice();
            return false;
        }
    }

    state_ = SourceState::kReady;
    return true;
}

bool CameraSource::Start()
{
    if (is_running_.load())
    {
        return true;
    }

    if (capture_thread_.joinable() || monitor_thread_.joinable())
    {
        Stop();
    }

    const bool has_capture_buffers = (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
                                         ? !mplane_probe_buffers_.empty()
                                         : !buffers_.empty();
    if (!config_.IsValid() || device_fd_ < 0 || !has_capture_buffers)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "CameraSource not initialized");
        return false;
    }

    if (!StartStream())
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(requeue_context_->mutex);
        requeue_context_->device_fd = device_fd_;
        requeue_context_->active = true;
    }

    is_running_ = true;
    frame_count_ = 0;
    dropped_frames_ = 0;
    disconnected_ = false;
    capture_thread_exited_ = false;
    monitor_running_ = true;

    capture_thread_ = std::thread(&CameraSource::CaptureLoop, this);
    monitor_thread_ = std::thread(&CameraSource::MonitorLoop, this);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = SourceState::kStreaming;
    }
    return true;
}

void CameraSource::Stop()
{
    const bool has_worker_threads = capture_thread_.joinable() || monitor_thread_.joinable();
    const SourceState current_state = state_.load();
    if (!is_running_.load() && !has_worker_threads && current_state != SourceState::kRetrying &&
        current_state != SourceState::kPermanentFailure &&
        current_state != SourceState::kDisconnected)
    {
        return;
    }

    is_running_ = false;
    monitor_running_ = false;
    monitor_cv_.notify_one();

    if (monitor_thread_.joinable())
    {
        monitor_thread_.join();
    }

    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(requeue_context_->mutex);
        requeue_context_->active = false;
    }

    StopStream();

    // 降级后恢复原始帧率，避免污染驱动状态和下一次启动
    // 放在 STREAMOFF 之后：部分驱动在 streaming 状态下拒绝 S_PARM
    if (is_degraded_.load() && device_fd_ >= 0)
    {
        const uint32_t actual_fps = SetRequestedFps(config_.fps_);
        if (actual_fps == 0)
        {
            platform::PlatformLogger::Log(
                core::LogLevel::kWarning, "camera_source",
                "Stop() restore fps failed");
        }
    }

    is_degraded_.store(false);
    current_target_fps_.store(0);
    window_start_ns_ = 0;
    window_frame_count_ = 0;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = SourceState::kIdle;
    }
}

bool CameraSource::IsRunning() const
{
    return is_running_.load();
}

void CameraSource::SetFrameCallback(FrameCallback callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = std::move(callback);
}

void CameraSource::SetFrameCallbackWithBuffer(FrameCallbackWithBuffer callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_with_buffer_ = std::move(callback);
}

void CameraSource::SetFramePacketCallback(FramePacketCallback callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_packet_callback_ = std::move(callback);
    has_frame_packet_callback_ = static_cast<bool>(frame_packet_callback_);
}

void CameraSource::SetDevicePath(const std::string& device_path)
{
    if (is_running_)
    {
        return;
    }
    device_path_ = device_path;
}

std::string CameraSource::GetDevicePath() const
{
    return device_path_;
}

void CameraSource::SetStreamIdentity(const core::CameraStreamIdentity& identity)
{
    if (is_running_ || !core::IsCameraStreamIdentityValid(identity))
    {
        return;
    }
    stream_identity_ = identity;
}

core::CameraStreamIdentity CameraSource::GetStreamIdentity() const
{
    return stream_identity_;
}

core::CameraConfig CameraSource::GetConfig() const
{
    return config_;
}

uint64_t CameraSource::GetFrameCount() const
{
    return frame_count_.load();
}

uint64_t CameraSource::GetDroppedFrameCount() const
{
    return dropped_frames_.load();
}

bool CameraSource::IsDmaBufPathEnabled() const
{
    return dma_buf_path_enabled_;
}

uint64_t CameraSource::GetDmaBufFrameCount() const
{
    return dma_buf_frame_count_.load();
}

uint64_t CameraSource::GetDmaBufExportFailureCount() const
{
    return dma_buf_export_failures_.load();
}

uint64_t CameraSource::GetDmaBufLeaseExhaustedCount() const
{
    return lease_exhausted_count_.load();
}

size_t CameraSource::GetDmaBufActiveLeaseCount() const
{
    return requeue_context_ ? requeue_context_->active_leases.load() : 0;
}

size_t CameraSource::GetDmaBufLeaseInFlightMax() const
{
    return global_lease_in_flight_max_;
}

size_t CameraSource::GetDmaBufMinQueuedCaptureBuffers() const
{
    return min_queued_capture_buffers_;
}

SourceState CameraSource::GetState() const
{
    return state_.load();
}

uint64_t CameraSource::GetDisconnectionCount() const
{
    return disconnection_count_.load();
}

uint64_t CameraSource::GetRecoveryAttemptCount() const
{
    return recovery_attempt_count_.load();
}

void CameraSource::SetRecoveryFailedHook(RecoveryFailedCallback callback)
{
    std::lock_guard<std::mutex> lock(hook_mutex_);
    recovery_failed_hook_ = std::move(callback);
}

bool CameraSource::IsDegraded() const
{
    return is_degraded_.load();
}

uint64_t CameraSource::GetDegradationCount() const
{
    return degradation_count_.load();
}

uint64_t CameraSource::GetDegradationRecoveryCount() const
{
    return degradation_recovery_count_.load();
}

uint64_t CameraSource::GetDegradationFailureCount() const
{
    return degradation_failure_count_.load();
}

uint32_t CameraSource::GetRequestedFps() const
{
    return config_.fps_;
}

uint32_t CameraSource::GetCurrentTargetFps() const
{
    return current_target_fps_.load();
}

void CameraSource::FillMetrics(core::StreamMetrics* metrics) const
{
    if (!metrics)
    {
        return;
    }

    metrics->capture_frame_count = frame_count_.load();
    metrics->capture_dropped_count = dropped_frames_.load();
    metrics->dma_buf_frame_count = dma_buf_frame_count_.load();
    metrics->lease_exhausted_count = lease_exhausted_count_.load();
    metrics->active_lease_count = GetDmaBufActiveLeaseCount();

    metrics->disconnection_count = disconnection_count_.load();
    metrics->recovery_attempt_count = recovery_attempt_count_.load();
    metrics->current_state = static_cast<uint32_t>(state_.load());

    metrics->source_degraded = is_degraded_.load();
    metrics->source_requested_fps = config_.fps_;
    metrics->source_current_target_fps = current_target_fps_.load();
    metrics->source_degradation_count = degradation_count_.load();
    metrics->source_degradation_recovery_count = degradation_recovery_count_.load();
    metrics->source_degradation_failure_count = degradation_failure_count_.load();
}

void CameraSource::CaptureLoop()
{
    uint32_t consecutive_errors = 0;

    while (is_running_.load())
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(device_fd_, &fds);

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int ret = select(device_fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            ++consecutive_errors;
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "select failed: %s (consecutive_errors=%u)",
                                          strerror(errno), consecutive_errors);

            const bool should_recover =
                CheckDisconnection(errno) || (config_.enable_auto_recovery &&
                                              consecutive_errors >= config_.disconnect_threshold);
            if (should_recover)
            {
                disconnected_.store(true);
                is_running_.store(false);
                break;
            }

            if (!config_.enable_auto_recovery)
            {
                is_running_.store(false);
                break;
            }
            continue;
        }

        if (ret == 0)
        {
            continue;
        }

        consecutive_errors = 0;

        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = static_cast<v4l2_buf_type>(CaptureBufferType());
        buf.memory = V4L2_MEMORY_MMAP;
        if (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
        {
            buf.length = mplane_probe_plane_count_;
            buf.m.planes = planes;
        }

        if (Xioctl(device_fd_, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN)
            {
                continue;
            }

            ++consecutive_errors;
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "VIDIOC_DQBUF failed: %s (consecutive_errors=%u)",
                                          strerror(errno), consecutive_errors);

            const bool should_recover =
                CheckDisconnection(errno) || (config_.enable_auto_recovery &&
                                              consecutive_errors >= config_.disconnect_threshold);
            if (should_recover)
            {
                disconnected_.store(true);
                is_running_.store(false);
                break;
            }

            if (!config_.enable_auto_recovery)
            {
                is_running_.store(false);
                break;
            }
            continue;
        }

        consecutive_errors = 0;
        HandleDequeuedBuffer(buf, planes);
    }

    capture_thread_exited_.store(true);
    monitor_cv_.notify_one();
}

bool CameraSource::CheckDisconnection(int error_code) const
{
    switch (error_code)
    {
        case ENODEV:
        case ENXIO:
        case EIO:
            return true;
        default:
            return false;
    }
}

void CameraSource::MonitorLoop()
{
    while (monitor_running_.load())
    {
        {
            std::unique_lock<std::mutex> lock(monitor_mutex_);
            monitor_cv_.wait(lock, [this]()
                             { return !monitor_running_.load() || capture_thread_exited_.load(); });
        }

        if (!monitor_running_.load())
        {
            break;
        }

        if (!capture_thread_exited_.exchange(false))
        {
            continue;
        }

        if (capture_thread_.joinable())
        {
            capture_thread_.join();
        }

        if (!disconnected_.exchange(false))
        {
            state_ = SourceState::kIdle;
            is_running_ = false;
            break;
        }

        HandleDisconnection();

        if (!config_.enable_auto_recovery)
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_ = SourceState::kIdle;
            is_running_ = false;
            break;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_ = SourceState::kRetrying;
        }

        uint32_t attempt = 0;
        uint32_t backoff_ms = config_.recovery_backoff_base_ms;

        while (monitor_running_.load() && attempt < config_.max_recovery_attempts)
        {
            {
                std::unique_lock<std::mutex> lock(monitor_mutex_);
                if (monitor_cv_.wait_for(lock, std::chrono::milliseconds(backoff_ms),
                                         [this]() { return !monitor_running_.load(); }))
                {
                    break;
                }
            }

            platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                          "Recovery attempt %u/%u: stream=%s device=%s",
                                          attempt + 1, config_.max_recovery_attempts,
                                          stream_identity_.stream_id.data(), device_path_.c_str());

            if (TryRecover())
            {
                platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                              "Recovery successful: stream=%s device=%s",
                                              stream_identity_.stream_id.data(),
                                              device_path_.c_str());

                std::lock_guard<std::mutex> lock(state_mutex_);
                state_ = SourceState::kStreaming;
                recovery_attempt_count_.fetch_add(attempt + 1);
                is_running_ = true;
                capture_thread_ = std::thread(&CameraSource::CaptureLoop, this);
                break;
            }

            ++attempt;
            backoff_ms = std::min(backoff_ms * 2, config_.recovery_backoff_max_ms);
        }

        if (!monitor_running_.load())
        {
            break;
        }

        if (attempt >= config_.max_recovery_attempts)
        {
            // M3: 在进入 permanent failure 前调用 recovery failed hook
            RecoveryFailedCallback recovery_failed_hook;
            {
                std::lock_guard<std::mutex> lock(hook_mutex_);
                recovery_failed_hook = recovery_failed_hook_;
            }
            if (recovery_failed_hook)
            {
                recovery_failed_hook(device_path_);
            }

            // 兼容同步 hook：publisher 当前使用异步重绑定，通常会先进入 kPermanentFailure，
            // 后台线程完成重绑定后再重新进入 kStreaming。
            if (state_.load() == SourceState::kStreaming)
            {
                break;
            }

            platform::PlatformLogger::Log(
                core::LogLevel::kError, "camera_source",
                "Recovery failed after %u attempts, entering permanent failure", attempt);

            std::lock_guard<std::mutex> lock(state_mutex_);
            state_ = SourceState::kPermanentFailure;
            is_running_ = false;
            break;
        }
    }
}

void CameraSource::HandleDisconnection()
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (state_.load() != SourceState::kStreaming)
    {
        return;
    }

    state_ = SourceState::kDisconnected;
    disconnection_count_.fetch_add(1);

    StopStream();
    CleanupBuffers();
    CleanupDmaBufExports();
    CleanupMPlaneProbeBuffers();
    CloseDevice();

    {
        std::lock_guard<std::mutex> lock(requeue_context_->mutex);
        requeue_context_->active = false;
        requeue_context_->device_fd = -1;
    }

    platform::PlatformLogger::Log(
        core::LogLevel::kWarning, "camera_source",
        "Device disconnected: stream=%s device=%s disconnection_count=%" PRIu64,
        stream_identity_.stream_id.data(), device_path_.c_str(), disconnection_count_.load());
}

bool CameraSource::TryRecover()
{
    if (!OpenDevice())
    {
        return false;
    }

    if (!SelectCaptureBufferType(device_capabilities_))
    {
        CloseDevice();
        return false;
    }

    bool init_ok = false;
    if (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
    {
        init_ok = InitMPlaneBuffers();
    }
    else
    {
        if (ConfigureDevice() && InitMMap())
        {
            pool_buffer_size_ = 0;
            if (!buffers_.empty())
            {
                pool_buffer_size_ = buffers_[0].length;
            }
            if (pool_buffer_size_ == 0)
            {
                pool_buffer_size_ = CalculateBufferSize(config_);
            }
            init_ok = buffer_pool_.Initialize(config_.buffer_count_, pool_buffer_size_);
        }
    }

    if (!init_ok)
    {
        CleanupDmaBufExports();
        CleanupMPlaneProbeBuffers();
        CleanupBuffers();
        CloseDevice();
        return false;
    }

    if (!StartStream())
    {
        StopStream();
        CleanupDmaBufExports();
        CleanupMPlaneProbeBuffers();
        CleanupBuffers();
        CloseDevice();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(requeue_context_->mutex);
        requeue_context_->device_fd = device_fd_;
        requeue_context_->active = true;
    }

    return true;
}

uint32_t CameraSource::SetRequestedFps(uint32_t fps)
{
    if (device_fd_ < 0)
    {
        return 0;
    }

    struct v4l2_streamparm parm;
    BuildStreamParm(&parm, CaptureBufferType(), fps);

    if (Xioctl(device_fd_, VIDIOC_S_PARM, &parm) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                      "VIDIOC_S_PARM failed for fps=%u: %s", fps,
                                      strerror(errno));
        return 0;
    }

    // 读回实际值，驱动可能不支持精确匹配
    uint32_t actual_fps = fps;
    if (Xioctl(device_fd_, VIDIOC_G_PARM, &parm) >= 0)
    {
        actual_fps = ExtractFpsFromParm(parm);
        platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                      "Set requested fps=%u, actual fps=%u", fps,
                                      actual_fps);
    }
    else
    {
        platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                      "Set requested fps=%u", fps);
    }

    return actual_fps;
}

void CameraSource::UpdateDegradationState()
{
    UpdateDegradationStateAt(GetTimestampNs());
}

// UpdateDegradationStateAt 仅在 CaptureLoop 成功 DQBUF 并处理完成后调用。
// select timeout / EAGAIN / 错误帧 不推进窗口，也不计入帧数。
// 原因：2 秒以上无帧属于设备/驱动异常（ARCH-007 范畴），不是“维持不了目标帧率”。
// 长时间无帧时窗口数据会过时，但下一帧到来后 elapsed_ns 会远超 window_ns，
// 立即触发评估且 observed_fps 极低，从而正确触发降级（如果设备仍存活）。
void CameraSource::UpdateDegradationStateAt(uint64_t now_ns)
{
    if (!config_.enable_degradation)
    {
        return;
    }

    if (window_start_ns_ == 0)
    {
        window_start_ns_ = now_ns;
        window_frame_count_ = 1;
        return;
    }

    ++window_frame_count_;

    if (!is_degraded_.load())
    {
        const uint64_t window_ns =
            static_cast<uint64_t>(config_.degradation_window_sec) * 1000000000ULL;
        if (now_ns - window_start_ns_ < window_ns)
        {
            return;
        }

        const double elapsed_sec = static_cast<double>(now_ns - window_start_ns_) / 1000000000.0;
        const double observed_fps = static_cast<double>(window_frame_count_) / elapsed_sec;
        const double threshold = static_cast<double>(config_.fps_) * 0.6;

        if (observed_fps < threshold)
        {
            ApplyDegradation();
        }

        window_start_ns_ = now_ns;
        window_frame_count_ = 0;
    }
    else
    {
        if (window_frame_count_ < config_.degradation_recovery_frames)
        {
            return;
        }

        const double elapsed_sec = static_cast<double>(now_ns - window_start_ns_) / 1000000000.0;
        const double observed_fps = static_cast<double>(window_frame_count_) / elapsed_sec;
        // 用 current_target_fps_ 判断稳定，而不是原始 fps。
        // 降级到 15fps 后，observed >= 12 即认为稳定，执行 trial restore 到原始 fps。
        // 恢复后重新开窗口观察，若又跌破阈值则再次降级。
        const double threshold = static_cast<double>(current_target_fps_.load()) * 0.8;

        if (observed_fps >= threshold)
        {
            RestoreOriginalFps();
        }

        window_start_ns_ = now_ns;
        window_frame_count_ = 0;
    }
}

bool CameraSource::ApplyDegradation()
{
    if (is_degraded_.load())
    {
        return true;
    }

    if (config_.fps_ <= config_.degradation_target_fps)
    {
        return false;
    }

    const uint32_t reported_fps = ReconfigureFpsInLoop(config_.degradation_target_fps);
    if (reported_fps == 0)
    {
        degradation_failure_count_.fetch_add(1);
        return false;
    }

    is_degraded_.store(true);
    // 用策略目标 fps 驱动状态机，不依赖驱动回读（部分驱动 G_PARM 不可信）
    current_target_fps_.store(config_.degradation_target_fps);
    degradation_count_.fetch_add(1);

    platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                  "Degraded to target=%u driver_reported=%u fps: "
                                  "stream=%s device=%s",
                                  config_.degradation_target_fps, reported_fps,
                                  stream_identity_.stream_id.data(),
                                  device_path_.c_str());
    return true;
}

bool CameraSource::RestoreOriginalFps()
{
    if (!is_degraded_.load())
    {
        return true;
    }

    const uint32_t reported_fps = ReconfigureFpsInLoop(config_.fps_);
    if (reported_fps == 0)
    {
        degradation_failure_count_.fetch_add(1);
        return false;
    }

    is_degraded_.store(false);
    // 用策略目标 fps 驱动状态机，不依赖驱动回读
    current_target_fps_.store(config_.fps_);
    degradation_recovery_count_.fetch_add(1);

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                  "Restored to target=%u driver_reported=%u fps: "
                                  "stream=%s device=%s",
                                  config_.fps_, reported_fps,
                                  stream_identity_.stream_id.data(),
                                  device_path_.c_str());
    return true;
}

uint32_t CameraSource::ReconfigureFpsInLoop(uint32_t fps)
{
    if (device_fd_ < 0)
    {
        return 0;
    }

    // DMA-BUF 路径：存在未 release 的 lease 时跳过重配置，
    // 避免 RequeueAllBuffers 和 subscriber 持有的 fd 冲突。
    if (dma_buf_path_enabled_ && requeue_context_ &&
        requeue_context_->active_leases.load() > 0)
    {
        platform::PlatformLogger::Log(
            core::LogLevel::kWarning, "camera_source",
            "Skip fps reconfigure: %zu active DMA-BUF leases",
            requeue_context_->active_leases.load());
        return 0;
    }

    const bool was_streaming = streaming_;
    if (was_streaming)
    {
        StopStream();
    }

    struct v4l2_streamparm parm;
    BuildStreamParm(&parm, CaptureBufferType(), fps);

    if (Xioctl(device_fd_, VIDIOC_S_PARM, &parm) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                      "VIDIOC_S_PARM failed for fps=%u: %s", fps,
                                      strerror(errno));
        // 尽力恢复：恢复原 fps 并重新启动采集
        BuildStreamParm(&parm, CaptureBufferType(), config_.fps_);
        Xioctl(device_fd_, VIDIOC_S_PARM, &parm);
        if (was_streaming)
        {
            RequeueAllBuffers();
            StartStream();
        }
        return 0;
    }

    uint32_t actual_fps = fps;
    if (Xioctl(device_fd_, VIDIOC_G_PARM, &parm) >= 0)
    {
        actual_fps = ExtractFpsFromParm(parm);
    }

    if (was_streaming)
    {
        if (!RequeueAllBuffers() || !StartStream())
        {
            platform::PlatformLogger::Log(
                core::LogLevel::kError, "camera_source",
                "Failed to restart stream after fps reconfigure, attempting rollback");
            BuildStreamParm(&parm, CaptureBufferType(), config_.fps_);
            Xioctl(device_fd_, VIDIOC_S_PARM, &parm);
            RequeueAllBuffers();
            StartStream();
            return 0;
        }
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                  "Reconfigured fps to target=%u actual=%u", fps, actual_fps);
    return actual_fps;
}

bool CameraSource::RequeueAllBuffers()
{
    if (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
    {
        for (auto& buffer : mplane_probe_buffers_)
        {
            struct v4l2_buffer qbuf;
            struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
            std::memset(&qbuf, 0, sizeof(qbuf));
            std::memset(qplanes, 0, sizeof(qplanes));
            qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            qbuf.memory = V4L2_MEMORY_MMAP;
            qbuf.index = buffer.index;
            qbuf.length = mplane_probe_plane_count_;
            qbuf.m.planes = qplanes;
            for (uint32_t p = 0; p < mplane_probe_plane_count_ && p < VIDEO_MAX_PLANES; ++p)
            {
                qplanes[p].length = buffer.planes[p].length;
            }
            if (Xioctl(device_fd_, VIDIOC_QBUF, &qbuf) < 0)
            {
                platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                              "RequeueAllBuffers QBUF failed: %s",
                                              strerror(errno));
                return false;
            }
        }
    }
    else
    {
        for (uint32_t i = 0; i < buffers_.size(); ++i)
        {
            struct v4l2_buffer qbuf;
            std::memset(&qbuf, 0, sizeof(qbuf));
            qbuf.type = static_cast<v4l2_buf_type>(CaptureBufferType());
            qbuf.memory = V4L2_MEMORY_MMAP;
            qbuf.index = i;
            if (Xioctl(device_fd_, VIDIOC_QBUF, &qbuf) < 0)
            {
                platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                              "RequeueAllBuffers QBUF failed: %s",
                                              strerror(errno));
                return false;
            }
        }
    }
    return true;
}

void CameraSource::HandleDequeuedBuffer(struct v4l2_buffer& buf, struct v4l2_plane* planes)
{
    const size_t buffer_count = (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
                                    ? mplane_probe_buffers_.size()
                                    : buffers_.size();
    if (buf.index >= buffer_count)
    {
        dropped_frames_.fetch_add(1);
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "Dequeued invalid buffer index: %u", buf.index);
        return;
    }

    if (ShouldUseDmaBufPath() && HandleDequeuedBufferDmaBuf(buf, planes))
    {
        UpdateDegradationState();
        return;
    }

    HandleDequeuedBufferCopy(buf, planes);
    UpdateDegradationState();
}

void CameraSource::HandleDequeuedBufferCopy(struct v4l2_buffer& buf, struct v4l2_plane* planes)
{
    (void)planes;
    auto buffer_ref = buffer_pool_.Acquire();
    if (!buffer_ref)
    {
        dropped_frames_.fetch_add(1);
        RequeueBuffer(buf.index);
        return;
    }

    core::FrameHandle frame;
    frame.Reset();

    const uint64_t frame_id = frame_count_.fetch_add(1);
    frame.frame_id_ = static_cast<uint32_t>(frame_id);
    frame.camera_id_ = stream_identity_.camera_id;
    frame.timestamp_ns_ = GetTimestampNs();
    frame.width_ = config_.width_;
    frame.height_ = config_.height_;
    frame.format_ = config_.format_;
    frame.sequence_ = static_cast<uint32_t>(buf.sequence);
    frame.memory_type_ = core::MemoryType::kHeap;

    if (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
    {
        // MPLANE 第一版不做 copy fallback，直接丢帧
        dropped_frames_.fetch_add(1);
        RequeueBuffer(buf.index);
        return;
    }

    size_t used_size = static_cast<size_t>(buf.bytesused);
    if (used_size == 0)
    {
        used_size = buffers_[buf.index].length;
    }
    void* src_data = buffers_[buf.index].start;

    const size_t copy_size = std::min(used_size, buffer_ref->Size());
    std::memcpy(buffer_ref->Data(), src_data, copy_size);

    frame.virtual_address_ = buffer_ref->Data();
    frame.buffer_size_ = copy_size;

    FillFrameLayout(frame, copy_size);

    FrameCallback callback;
    FrameCallbackWithBuffer callback_with_buffer;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = callback_;
        callback_with_buffer = callback_with_buffer_;
    }

    if (callback_with_buffer)
    {
        callback_with_buffer(frame, buffer_ref);
    }
    else if (callback)
    {
        callback(frame);
    }

    RequeueBuffer(buf.index);
}

bool CameraSource::HandleDequeuedBufferDmaBuf(struct v4l2_buffer& buf, struct v4l2_plane* planes)
{
    const bool is_mplane = (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

    if (is_mplane)
    {
        if (buf.index >= mplane_probe_buffers_.size())
        {
            return false;
        }
        MPlaneProbeBuffer& mplane_buffer = mplane_probe_buffers_[buf.index];
        for (auto& plane : mplane_buffer.planes)
        {
            if (!plane.dma_buf_exported || plane.dma_buf_fd < 0)
            {
                return false;
            }
        }
    }
    else
    {
        Buffer& buffer = buffers_[buf.index];
        if (!buffer.dma_buf_exported || buffer.dma_buf_fd < 0)
        {
            return false;
        }
    }

    const size_t active_leases = requeue_context_->active_leases.load();
    if (active_leases >= global_lease_in_flight_max_)
    {
        lease_exhausted_count_.fetch_add(1);
        dropped_frames_.fetch_add(1);
        RequeueBuffer(buf.index);
        return true;
    }

    FramePacketCallback frame_packet_callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        frame_packet_callback = frame_packet_callback_;
    }
    if (!frame_packet_callback)
    {
        return false;
    }

    const uint64_t frame_id = frame_count_.fetch_add(1);
    dma_buf_frame_count_.fetch_add(1);

    core::FrameHandle frame;
    frame.Reset();
    frame.frame_id_ = static_cast<uint32_t>(frame_id);
    frame.camera_id_ = stream_identity_.camera_id;
    frame.timestamp_ns_ = GetTimestampNs();
    frame.width_ = config_.width_;
    frame.height_ = config_.height_;
    frame.format_ = config_.format_;
    frame.sequence_ = static_cast<uint32_t>(buf.sequence);
    frame.memory_type_ = core::MemoryType::kDmaBuf;
    frame.virtual_address_ = nullptr;
    FillFrameLayout(frame, 0);

    core::FrameDescriptor descriptor;
    descriptor.frame_id = frame_id;
    descriptor.camera_id = frame.camera_id_;
    descriptor.stream_id = stream_identity_.stream_id;
    descriptor.timestamp_ns = frame.timestamp_ns_;
    descriptor.sequence = frame.sequence_;
    descriptor.width = frame.width_;
    descriptor.height = frame.height_;
    descriptor.pixel_format = frame.format_;
    descriptor.memory_type = core::MemoryType::kDmaBuf;
    descriptor.buffer_id = buf.index;
    descriptor.flags = frame.flags_;

    if (is_mplane)
    {
        MPlaneProbeBuffer& mplane_buffer = mplane_probe_buffers_[buf.index];
        descriptor.plane_count = static_cast<uint32_t>(mplane_buffer.planes.size());

        // fd 去重：如果多个 plane 共享同一个 fd，只填一次
        int unique_fds[core::kMaxFrameFds];
        uint32_t fd_index_map[core::kMaxFramePlanes];
        uint32_t unique_fd_count = 0;
        for (size_t p = 0; p < mplane_buffer.planes.size() && p < core::kMaxFramePlanes; ++p)
        {
            int fd = mplane_buffer.planes[p].dma_buf_fd;
            uint32_t idx = 0;
            for (; idx < unique_fd_count && idx < core::kMaxFrameFds; ++idx)
            {
                if (unique_fds[idx] == fd)
                {
                    break;
                }
            }
            if (idx == unique_fd_count && unique_fd_count < core::kMaxFrameFds)
            {
                unique_fds[unique_fd_count] = fd;
                ++unique_fd_count;
            }
            fd_index_map[p] = idx;
        }
        descriptor.fd_count = unique_fd_count;
        for (uint32_t i = 0; i < unique_fd_count && i < core::kMaxFrameFds; ++i)
        {
            descriptor.fds[i] = unique_fds[i];
        }

        size_t total_bytes_used = 0;
        for (size_t p = 0; p < mplane_buffer.planes.size() && p < core::kMaxFramePlanes; ++p)
        {
            descriptor.planes[p].fd_index = fd_index_map[p];
            descriptor.planes[p].offset = mplane_buffer.planes[p].data_offset;
            descriptor.planes[p].stride = mplane_buffer.planes[p].bytes_per_line;
            descriptor.planes[p].length = mplane_buffer.planes[p].length;
            if (planes)
            {
                descriptor.planes[p].bytes_used = planes[p].bytesused;
            }
            else
            {
                descriptor.planes[p].bytes_used = mplane_buffer.planes[p].length;
            }
            total_bytes_used += descriptor.planes[p].bytes_used;
        }
        descriptor.total_bytes_used = total_bytes_used;
        frame.buffer_size_ = total_bytes_used;
        frame.buffer_fd_ = (unique_fd_count > 0) ? unique_fds[0] : -1;
        FillFrameLayout(frame, total_bytes_used);
    }
    else
    {
        Buffer& buffer = buffers_[buf.index];
        size_t used_size = static_cast<size_t>(buf.bytesused);
        if (used_size == 0)
        {
            used_size = buffer.length;
        }

        frame.buffer_fd_ = buffer.dma_buf_fd;
        frame.buffer_size_ = used_size;
        FillFrameLayout(frame, used_size);

        descriptor.plane_count = frame.plane_count_;
        descriptor.fd_count = 1;
        descriptor.fds[0] = buffer.dma_buf_fd;
        descriptor.total_bytes_used = used_size;
        for (uint32_t i = 0; i < frame.plane_count_ && i < core::kMaxFramePlanes; ++i)
        {
            descriptor.planes[i].fd_index = 0;
            descriptor.planes[i].offset = frame.plane_offset_[i];
            descriptor.planes[i].stride = frame.line_stride_[i];
            descriptor.planes[i].length = frame.plane_size_[i];
            descriptor.planes[i].bytes_used = frame.plane_size_[i];
        }
    }

    std::weak_ptr<RequeueContext> weak_context = requeue_context_;
    requeue_context_->active_leases.fetch_add(1);
    auto lease = std::make_shared<core::DmaBufFrameLease>(
        buf.index,
        [weak_context](uint32_t buffer_index)
        {
            if (auto context = weak_context.lock())
            {
                size_t current = context->active_leases.load();
                while (current > 0 &&
                       !context->active_leases.compare_exchange_weak(current, current - 1))
                {
                }

                std::lock_guard<std::mutex> lock(context->mutex);
                if (!context->active || context->device_fd < 0)
                {
                    return;
                }

                if (context->buffer_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
                {
                    struct v4l2_buffer qbuf;
                    struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
                    std::memset(&qbuf, 0, sizeof(qbuf));
                    std::memset(qplanes, 0, sizeof(qplanes));
                    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                    qbuf.memory = V4L2_MEMORY_MMAP;
                    qbuf.index = buffer_index;
                    qbuf.length = context->plane_count;
                    qbuf.m.planes = qplanes;
                    for (uint32_t p = 0; p < context->plane_count && p < VIDEO_MAX_PLANES; ++p)
                    {
                        qplanes[p].length = context->plane_lengths[p];
                    }
                    if (Xioctl(context->device_fd, VIDIOC_QBUF, &qbuf) < 0)
                    {
                        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                                      "VIDIOC_QBUF failed: %s", strerror(errno));
                    }
                }
                else
                {
                    struct v4l2_buffer qbuf;
                    std::memset(&qbuf, 0, sizeof(qbuf));
                    qbuf.type = static_cast<v4l2_buf_type>(context->buffer_type);
                    qbuf.memory = V4L2_MEMORY_MMAP;
                    qbuf.index = buffer_index;
                    if (Xioctl(context->device_fd, VIDIOC_QBUF, &qbuf) < 0)
                    {
                        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                                      "VIDIOC_QBUF failed: %s", strerror(errno));
                    }
                }
            }
        });

    core::FramePacket packet;
    packet.descriptor = descriptor;
    packet.handle = frame;
    packet.lease = lease;

    frame_packet_callback(packet);
    return true;
}

bool CameraSource::OpenDevice()
{
    if (device_fd_ >= 0)
    {
        return true;
    }

    device_fd_ = open(device_path_.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (device_fd_ < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "Failed to open %s: %s", device_path_.c_str(),
                                      strerror(errno));
        return false;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (Xioctl(device_fd_, VIDIOC_QUERYCAP, &cap) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "VIDIOC_QUERYCAP failed: %s", strerror(errno));
        CloseDevice();
        return false;
    }

    device_capabilities_ = EffectiveCapabilities(cap);
    if ((device_capabilities_ & V4L2_CAP_STREAMING) == 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "Device does not support streaming I/O");
        CloseDevice();
        return false;
    }

    return true;
}

void CameraSource::CloseDevice()
{
    if (device_fd_ >= 0)
    {
        close(device_fd_);
        device_fd_ = -1;
    }
}

bool CameraSource::SelectCaptureBufferType(uint32_t capabilities)
{
    const bool supports_single_plane = (capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0;
    const bool supports_mplane = (capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;

    if (supports_single_plane)
    {
        capture_buffer_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (requeue_context_)
        {
            requeue_context_->buffer_type = capture_buffer_type_;
        }
        platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                      "selected V4L2 single-planar capture backend "
                                      "(mplane_supported=%u)",
                                      supports_mplane ? 1U : 0U);
        return true;
    }

    if (supports_mplane)
    {
        capture_buffer_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (requeue_context_)
        {
            requeue_context_->buffer_type = capture_buffer_type_;
        }
        platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                      "selected V4L2 MPLANE capture backend");
        return true;
    }

    platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                  "Device does not support V4L2 video capture");
    return false;
}

bool CameraSource::ConfigureDevice()
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = static_cast<v4l2_buf_type>(CaptureBufferType());
    fmt.fmt.pix.width = config_.width_;
    fmt.fmt.pix.height = config_.height_;
    fmt.fmt.pix.pixelformat = ToV4L2PixelFormat(config_.format_);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (Xioctl(device_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "VIDIOC_S_FMT failed: %s", strerror(errno));
        return false;
    }

    const core::PixelFormat negotiated_format = FromV4L2PixelFormat(fmt.fmt.pix.pixelformat);
    config_.width_ = fmt.fmt.pix.width;
    config_.height_ = fmt.fmt.pix.height;
    if (negotiated_format != core::PixelFormat::kUnknown)
    {
        config_.format_ = negotiated_format;
    }

    platform::PlatformLogger::Log(
        core::LogLevel::kInfo, "camera_source",
        "negotiated format: width=%u height=%u fourcc=%c%c%c%c", config_.width_, config_.height_,
        fmt.fmt.pix.pixelformat & 0xff, (fmt.fmt.pix.pixelformat >> 8) & 0xff,
        (fmt.fmt.pix.pixelformat >> 16) & 0xff, (fmt.fmt.pix.pixelformat >> 24) & 0xff);

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = static_cast<v4l2_buf_type>(CaptureBufferType());
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = std::max<uint32_t>(1, config_.fps_);

    if (Xioctl(device_fd_, VIDIOC_S_PARM, &parm) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                      "VIDIOC_S_PARM failed: %s", strerror(errno));
    }

    return true;
}

bool CameraSource::InitMMap()
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = config_.buffer_count_;
    req.type = static_cast<v4l2_buf_type>(CaptureBufferType());
    req.memory = V4L2_MEMORY_MMAP;

    if (Xioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "VIDIOC_REQBUFS failed: %s", strerror(errno));
        return false;
    }

    if (req.count < 2)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "Insufficient buffer memory");
        return false;
    }

    buffers_.clear();
    buffers_.resize(req.count);

    for (uint32_t i = 0; i < req.count; ++i)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = static_cast<v4l2_buf_type>(CaptureBufferType());
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (Xioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "VIDIOC_QUERYBUF failed: %s", strerror(errno));
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start =
            mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd_, buf.m.offset);
        if (buffers_[i].start == MAP_FAILED)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "mmap failed: %s", strerror(errno));
            return false;
        }
    }

    dma_buf_path_enabled_ = false;
    dma_buf_frame_count_ = 0;
    dma_buf_export_failures_ = 0;
    lease_exhausted_count_ = 0;
    if (config_.io_method_ == static_cast<uint32_t>(core::IoMethod::kDmaBuf))
    {
        dma_buf_path_enabled_ = InitDmaBufExport();
        if (!dma_buf_path_enabled_)
        {
            platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                          "DMA-BUF export unavailable, falling back to MMAP copy");
        }
    }

    for (uint32_t i = 0; i < buffers_.size(); ++i)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = static_cast<v4l2_buf_type>(CaptureBufferType());
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (Xioctl(device_fd_, VIDIOC_QBUF, &buf) < 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "VIDIOC_QBUF failed: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

bool CameraSource::InitDmaBufExport()
{
    if (buffers_.empty())
    {
        return false;
    }

    bool all_exported = true;
    for (uint32_t i = 0; i < buffers_.size(); ++i)
    {
        struct v4l2_exportbuffer expbuf;
        std::memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = static_cast<v4l2_buf_type>(CaptureBufferType());
        expbuf.index = i;
        expbuf.plane = 0;
        expbuf.flags = O_CLOEXEC;

        if (Xioctl(device_fd_, VIDIOC_EXPBUF, &expbuf) < 0)
        {
            dma_buf_export_failures_.fetch_add(1);
            all_exported = false;
            platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                          "VIDIOC_EXPBUF failed for buffer %u: %s", i,
                                          strerror(errno));
            break;
        }

        buffers_[i].dma_buf_fd = expbuf.fd;
        buffers_[i].dma_buf_exported = true;
    }

    if (!all_exported)
    {
        CleanupDmaBufExports();
        return false;
    }

    min_queued_capture_buffers_ = buffers_.size() >= 4 ? 2 : 1;
    if (buffers_.size() <= min_queued_capture_buffers_)
    {
        global_lease_in_flight_max_ = 1;
    }
    else
    {
        global_lease_in_flight_max_ = buffers_.size() - min_queued_capture_buffers_;
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                  "DMA-BUF export enabled: actual_buffer_count=%zu "
                                  "min_queued_capture_buffers=%zu lease_in_flight_max=%zu",
                                  buffers_.size(), min_queued_capture_buffers_,
                                  global_lease_in_flight_max_);
    if (requeue_context_)
    {
        requeue_context_->buffer_type = CaptureBufferType();
    }
    return true;
}

bool CameraSource::ConfigureMPlaneFormatForProbe()
{
    struct v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = config_.width_;
    fmt.fmt.pix_mp.height = config_.height_;
    fmt.fmt.pix_mp.pixelformat = ToV4L2PixelFormat(config_.format_);
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (Xioctl(device_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "MPLANE VIDIOC_S_FMT failed: %s", strerror(errno));
        return false;
    }

    const uint32_t plane_count = fmt.fmt.pix_mp.num_planes;
    if (plane_count == 0 || plane_count > VIDEO_MAX_PLANES || plane_count > core::kMaxFramePlanes)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "MPLANE invalid plane count: %u", plane_count);
        return false;
    }

    mplane_probe_plane_count_ = plane_count;
    mplane_probe_width_ = fmt.fmt.pix_mp.width;
    mplane_probe_height_ = fmt.fmt.pix_mp.height;
    mplane_probe_fourcc_ = fmt.fmt.pix_mp.pixelformat;
    mplane_probe_strides_.clear();
    mplane_probe_strides_.reserve(mplane_probe_plane_count_);
    for (uint32_t i = 0; i < mplane_probe_plane_count_; ++i)
    {
        mplane_probe_strides_.push_back(fmt.fmt.pix_mp.plane_fmt[i].bytesperline);
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                  "MPLANE probe format: width=%u height=%u "
                                  "fourcc=%c%c%c%c planes=%u",
                                  mplane_probe_width_, mplane_probe_height_,
                                  mplane_probe_fourcc_ & 0xff, (mplane_probe_fourcc_ >> 8) & 0xff,
                                  (mplane_probe_fourcc_ >> 16) & 0xff,
                                  (mplane_probe_fourcc_ >> 24) & 0xff, mplane_probe_plane_count_);

    return true;
}

bool CameraSource::InitMPlaneBuffersForProbe()
{
    if (mplane_probe_plane_count_ == 0)
    {
        return false;
    }

    struct v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count = config_.buffer_count_;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (Xioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "MPLANE VIDIOC_REQBUFS failed: %s", strerror(errno));
        return false;
    }

    if (req.count < 2)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "MPLANE insufficient buffer memory");
        return false;
    }

    mplane_probe_buffers_.clear();
    mplane_probe_buffers_.resize(req.count);

    for (uint32_t i = 0; i < req.count; ++i)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = mplane_probe_plane_count_;
        buf.m.planes = planes;

        if (Xioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "MPLANE VIDIOC_QUERYBUF failed: %s", strerror(errno));
            return false;
        }

        MPlaneProbeBuffer& buffer = mplane_probe_buffers_[i];
        buffer.index = i;
        buffer.planes.resize(mplane_probe_plane_count_);
        for (uint32_t p = 0; p < mplane_probe_plane_count_; ++p)
        {
            MPlaneProbePlane& plane = buffer.planes[p];
            plane.bytes_per_line = p < mplane_probe_strides_.size() ? mplane_probe_strides_[p] : 0;
            plane.length = planes[p].length;
            plane.bytes_used = planes[p].bytesused;
            plane.data_offset = planes[p].data_offset;
            plane.mem_offset = planes[p].m.mem_offset;
        }
    }

    return true;
}

bool CameraSource::ExportMPlaneDmaBufsForProbe()
{
    if (mplane_probe_buffers_.empty() || mplane_probe_plane_count_ == 0)
    {
        return false;
    }

    for (auto& buffer : mplane_probe_buffers_)
    {
        for (uint32_t p = 0; p < buffer.planes.size(); ++p)
        {
            struct v4l2_exportbuffer expbuf;
            std::memset(&expbuf, 0, sizeof(expbuf));
            expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            expbuf.index = buffer.index;
            expbuf.plane = p;
            expbuf.flags = O_CLOEXEC;

            if (Xioctl(device_fd_, VIDIOC_EXPBUF, &expbuf) < 0)
            {
                platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                              "MPLANE VIDIOC_EXPBUF failed: index=%u "
                                              "plane=%u error=%s",
                                              buffer.index, p, strerror(errno));
                CleanupMPlaneProbeBuffers();
                return false;
            }

            buffer.planes[p].dma_buf_fd = expbuf.fd;
            buffer.planes[p].dma_buf_exported = true;
        }
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                  "MPLANE DMA-BUF export skeleton initialized: "
                                  "buffers=%zu planes=%u",
                                  mplane_probe_buffers_.size(), mplane_probe_plane_count_);
    return true;
}

bool CameraSource::InitMPlaneDmaBufExportSkeleton()
{
    CleanupMPlaneProbeBuffers();
    if (!ConfigureMPlaneFormatForProbe())
    {
        return false;
    }
    if (!InitMPlaneBuffersForProbe())
    {
        CleanupMPlaneProbeBuffers();
        return false;
    }
    return ExportMPlaneDmaBufsForProbe();
}

bool CameraSource::ShouldRunMPlaneProbeOnly() const
{
    const char* value = std::getenv("CAMERA_SUBSYSTEM_ENABLE_MPLANE_PROBE");
    if (value == nullptr)
    {
        return false;
    }

    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "on") == 0 ||
           std::strcmp(value, "ON") == 0;
}

bool CameraSource::InitMPlaneBuffers()
{
    CleanupMPlaneProbeBuffers();

    if (!ConfigureMPlaneFormatForProbe())
    {
        return false;
    }

    if (!InitMPlaneBuffersForProbe())
    {
        CleanupMPlaneProbeBuffers();
        return false;
    }

    if (config_.io_method_ == static_cast<uint32_t>(core::IoMethod::kDmaBuf))
    {
        if (!ExportMPlaneDmaBufsForProbe())
        {
            CleanupMPlaneProbeBuffers();
            return false;
        }
        dma_buf_path_enabled_ = true;
        min_queued_capture_buffers_ = mplane_probe_buffers_.size() >= 4 ? 2 : 1;
        if (mplane_probe_buffers_.size() <= min_queued_capture_buffers_)
        {
            global_lease_in_flight_max_ = 1;
        }
        else
        {
            global_lease_in_flight_max_ =
                mplane_probe_buffers_.size() - min_queued_capture_buffers_;
        }
    }
    else
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "MPLANE path requires IoMethod::kDmaBuf");
        CleanupMPlaneProbeBuffers();
        return false;
    }

    if (requeue_context_)
    {
        requeue_context_->buffer_type = capture_buffer_type_;
        requeue_context_->plane_count = mplane_probe_plane_count_;
        for (uint32_t p = 0; p < mplane_probe_plane_count_ && p < VIDEO_MAX_PLANES; ++p)
        {
            requeue_context_->plane_lengths[p] = mplane_probe_buffers_[0].planes[p].length;
        }
    }

    for (auto& buffer : mplane_probe_buffers_)
    {
        struct v4l2_buffer qbuf;
        struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
        std::memset(&qbuf, 0, sizeof(qbuf));
        std::memset(qplanes, 0, sizeof(qplanes));
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index = buffer.index;
        qbuf.length = mplane_probe_plane_count_;
        qbuf.m.planes = qplanes;
        for (uint32_t p = 0; p < mplane_probe_plane_count_; ++p)
        {
            qplanes[p].length = buffer.planes[p].length;
        }

        if (Xioctl(device_fd_, VIDIOC_QBUF, &qbuf) < 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "MPLANE VIDIOC_QBUF failed: index=%u error=%s",
                                          buffer.index, strerror(errno));
            CleanupMPlaneProbeBuffers();
            return false;
        }
    }

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                  "MPLANE live initialized: buffers=%zu planes=%u "
                                  "dma_buf_enabled=%u min_queued=%zu lease_max=%zu",
                                  mplane_probe_buffers_.size(), mplane_probe_plane_count_,
                                  dma_buf_path_enabled_ ? 1U : 0U, min_queued_capture_buffers_,
                                  global_lease_in_flight_max_);
    return true;
}

void CameraSource::CleanupDmaBufExports()
{
    if (requeue_context_ && requeue_context_->active_leases.load() > 0)
    {
        for (int i = 0; i < 20 && requeue_context_->active_leases.load() > 0; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const size_t remaining_leases = requeue_context_->active_leases.load();
        if (remaining_leases > 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                          "cleanup with %zu active DMA-BUF leases",
                                          remaining_leases);
        }
    }

    for (auto& buffer : buffers_)
    {
        if (buffer.dma_buf_fd >= 0)
        {
            close(buffer.dma_buf_fd);
        }
        buffer.dma_buf_fd = -1;
        buffer.dma_buf_exported = false;
    }
    dma_buf_path_enabled_ = false;
}

void CameraSource::CleanupMPlaneProbeBuffers()
{
    for (auto& buffer : mplane_probe_buffers_)
    {
        for (auto& plane : buffer.planes)
        {
            if (plane.dma_buf_fd >= 0)
            {
                close(plane.dma_buf_fd);
            }
            plane.dma_buf_fd = -1;
            plane.dma_buf_exported = false;
        }
    }

    if (device_fd_ >= 0 && !mplane_probe_buffers_.empty())
    {
        struct v4l2_requestbuffers req;
        std::memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_MMAP;
        req.count = 0;
        if (Xioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                          "MPLANE probe cleanup REQBUFS(0) failed: %s",
                                          strerror(errno));
        }
    }

    mplane_probe_buffers_.clear();
    mplane_probe_plane_count_ = 0;
    mplane_probe_width_ = 0;
    mplane_probe_height_ = 0;
    mplane_probe_fourcc_ = 0;
    mplane_probe_strides_.clear();
}

bool CameraSource::ShouldUseDmaBufPath() const
{
    if (!dma_buf_path_enabled_)
    {
        return false;
    }

    return has_frame_packet_callback_.load();
}

void CameraSource::RequeueBuffer(uint32_t buffer_index)
{
    if (capture_buffer_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
    {
        struct v4l2_buffer qbuf;
        struct v4l2_plane qplanes[VIDEO_MAX_PLANES];
        std::memset(&qbuf, 0, sizeof(qbuf));
        std::memset(qplanes, 0, sizeof(qplanes));
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index = buffer_index;
        qbuf.length = mplane_probe_plane_count_;
        qbuf.m.planes = qplanes;
        for (uint32_t p = 0; p < mplane_probe_plane_count_ && p < VIDEO_MAX_PLANES; ++p)
        {
            if (buffer_index < mplane_probe_buffers_.size())
            {
                qplanes[p].length = mplane_probe_buffers_[buffer_index].planes[p].length;
            }
        }

        if (Xioctl(device_fd_, VIDIOC_QBUF, &qbuf) < 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "VIDIOC_QBUF failed: %s", strerror(errno));
        }
    }
    else
    {
        struct v4l2_buffer qbuf;
        std::memset(&qbuf, 0, sizeof(qbuf));
        qbuf.type = static_cast<v4l2_buf_type>(CaptureBufferType());
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index = buffer_index;

        if (Xioctl(device_fd_, VIDIOC_QBUF, &qbuf) < 0)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "VIDIOC_QBUF failed: %s", strerror(errno));
        }
    }
}

bool CameraSource::StartStream()
{
    if (streaming_)
    {
        return true;
    }

    enum v4l2_buf_type type = static_cast<v4l2_buf_type>(CaptureBufferType());
    if (Xioctl(device_fd_, VIDIOC_STREAMON, &type) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "VIDIOC_STREAMON failed: %s", strerror(errno));
        return false;
    }

    streaming_ = true;
    return true;
}

void CameraSource::StopStream()
{
    if (!streaming_)
    {
        return;
    }

    enum v4l2_buf_type type = static_cast<v4l2_buf_type>(CaptureBufferType());
    if (Xioctl(device_fd_, VIDIOC_STREAMOFF, &type) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                      "VIDIOC_STREAMOFF failed: %s", strerror(errno));
    }
    streaming_ = false;
}

void CameraSource::CleanupBuffers()
{
    for (auto& buffer : buffers_)
    {
        if (buffer.start && buffer.length > 0)
        {
            munmap(buffer.start, buffer.length);
        }
        buffer.start = nullptr;
        buffer.length = 0;
    }
    buffers_.clear();
}

size_t CameraSource::CalculateBufferSize(const core::CameraConfig& config) const
{
    const uint32_t width = config.width_;
    const uint32_t height = config.height_;

    switch (config.format_)
    {
        case core::PixelFormat::kNV12:
            return width * height * 3 / 2;
        case core::PixelFormat::kYUYV:
            return width * height * 2;
        case core::PixelFormat::kRGB888:
            return width * height * 3;
        case core::PixelFormat::kRGBA8888:
            return width * height * 4;
        case core::PixelFormat::kMJPEG:
        case core::PixelFormat::kH264:
        case core::PixelFormat::kH265:
        default:
            return width * height * 2;
    }
}

void CameraSource::FillFrameLayout(core::FrameHandle& frame, size_t buffer_size) const
{
    const uint32_t width = frame.width_;
    const uint32_t height = frame.height_;

    if (frame.format_ == core::PixelFormat::kNV12)
    {
        frame.plane_count_ = 2;
        frame.line_stride_[0] = width;
        frame.line_stride_[1] = width;
        frame.plane_offset_[0] = 0;
        frame.plane_offset_[1] = width * height;
        frame.plane_size_[0] = width * height;
        frame.plane_size_[1] = width * height / 2;
    }
    else
    {
        frame.plane_count_ = 1;
        frame.line_stride_[0] = width * 2;
        frame.plane_offset_[0] = 0;
        frame.plane_size_[0] = static_cast<uint32_t>(buffer_size);
    }
}

uint64_t CameraSource::GetTimestampNs() const
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

uint32_t CameraSource::CaptureBufferType() const
{
    return capture_buffer_type_;
}

uint32_t CameraSource::ToV4L2PixelFormat(core::PixelFormat format) const
{
    switch (format)
    {
        case core::PixelFormat::kNV12:
            return V4L2_PIX_FMT_NV12;
        case core::PixelFormat::kYUYV:
            return V4L2_PIX_FMT_YUYV;
        case core::PixelFormat::kRGB888:
            return V4L2_PIX_FMT_RGB24;
        case core::PixelFormat::kRGBA8888:
            return V4L2_PIX_FMT_RGB32;
        case core::PixelFormat::kMJPEG:
            return V4L2_PIX_FMT_MJPEG;
        case core::PixelFormat::kH264:
            return V4L2_PIX_FMT_H264;
        case core::PixelFormat::kH265:
            return V4L2_PIX_FMT_HEVC;
        case core::PixelFormat::kUnknown:
        default:
            return V4L2_PIX_FMT_NV12;
    }
}

core::PixelFormat CameraSource::FromV4L2PixelFormat(uint32_t format) const
{
    switch (format)
    {
        case V4L2_PIX_FMT_NV12:
            return core::PixelFormat::kNV12;
        case V4L2_PIX_FMT_YUYV:
            return core::PixelFormat::kYUYV;
        case V4L2_PIX_FMT_RGB24:
            return core::PixelFormat::kRGB888;
        case V4L2_PIX_FMT_RGB32:
            return core::PixelFormat::kRGBA8888;
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
            return core::PixelFormat::kMJPEG;
        case V4L2_PIX_FMT_H264:
            return core::PixelFormat::kH264;
        case V4L2_PIX_FMT_HEVC:
            return core::PixelFormat::kH265;
        default:
            return core::PixelFormat::kUnknown;
    }
}

} // namespace camera
} // namespace camera_subsystem
