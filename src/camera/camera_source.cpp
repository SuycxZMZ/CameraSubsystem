#include "camera_subsystem/camera/camera_source.h"

#include "camera_subsystem/core/frame_lease.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
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

} // namespace

CameraSource::CameraSource()
    : config_()
    , device_path_("/dev/video0")
    , device_fd_(-1)
    , streaming_(false)
    , requeue_context_(std::make_shared<RequeueContext>())
    , is_running_(false)
    , frame_count_(0)
    , dropped_frames_(0)
{
}

CameraSource::~CameraSource()
{
    Stop();
    CleanupDmaBufExports();
    CleanupBuffers();
    CloseDevice();
}

bool CameraSource::Initialize(const core::CameraConfig& config)
{
    Stop();
    CleanupDmaBufExports();
    CleanupBuffers();
    CloseDevice();

    if (!config.IsValid())
    {
        return false;
    }

    config_ = config;

    if (!OpenDevice())
    {
        return false;
    }

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

    return true;
}

bool CameraSource::Start()
{
    if (is_running_)
    {
        return true;
    }

    if (!config_.IsValid() || buffers_.empty())
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
    capture_thread_ = std::thread(&CameraSource::CaptureLoop, this);
    return true;
}

void CameraSource::Stop()
{
    if (!is_running_)
    {
        return;
    }

    is_running_ = false;
    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(requeue_context_->mutex);
        requeue_context_->active = false;
    }

    StopStream();
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

void CameraSource::CaptureLoop()
{
    while (is_running_)
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
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "select failed: %s", strerror(errno));
            break;
        }
        if (ret == 0)
        {
            continue;
        }

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (Xioctl(device_fd_, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN)
            {
                continue;
            }
            platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                          "VIDIOC_DQBUF failed: %s", strerror(errno));
            break;
        }

        HandleDequeuedBuffer(buf);
    }
}

void CameraSource::HandleDequeuedBuffer(struct v4l2_buffer& buf)
{
    if (buf.index >= buffers_.size())
    {
        dropped_frames_.fetch_add(1);
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "Dequeued invalid buffer index: %u", buf.index);
        return;
    }

    if (ShouldUseDmaBufPath() && HandleDequeuedBufferDmaBuf(buf))
    {
        return;
    }

    HandleDequeuedBufferCopy(buf);
}

void CameraSource::HandleDequeuedBufferCopy(struct v4l2_buffer& buf)
{
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
    frame.camera_id_ = 0;
    frame.timestamp_ns_ = GetTimestampNs();
    frame.width_ = config_.width_;
    frame.height_ = config_.height_;
    frame.format_ = config_.format_;
    frame.sequence_ = static_cast<uint32_t>(buf.sequence);
    frame.memory_type_ = core::MemoryType::kHeap;

    size_t used_size = static_cast<size_t>(buf.bytesused);
    if (used_size == 0)
    {
        used_size = buffers_[buf.index].length;
    }

    const size_t copy_size = std::min(used_size, buffer_ref->Size());
    std::memcpy(buffer_ref->Data(), buffers_[buf.index].start, copy_size);

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

bool CameraSource::HandleDequeuedBufferDmaBuf(struct v4l2_buffer& buf)
{
    Buffer& buffer = buffers_[buf.index];
    if (!buffer.dma_buf_exported || buffer.dma_buf_fd < 0)
    {
        return false;
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

    size_t used_size = static_cast<size_t>(buf.bytesused);
    if (used_size == 0)
    {
        used_size = buffer.length;
    }

    const uint64_t frame_id = frame_count_.fetch_add(1);

    core::FrameHandle frame;
    frame.Reset();
    frame.frame_id_ = static_cast<uint32_t>(frame_id);
    frame.camera_id_ = 0;
    frame.timestamp_ns_ = GetTimestampNs();
    frame.width_ = config_.width_;
    frame.height_ = config_.height_;
    frame.format_ = config_.format_;
    frame.sequence_ = static_cast<uint32_t>(buf.sequence);
    frame.memory_type_ = core::MemoryType::kDmaBuf;
    frame.buffer_fd_ = buffer.dma_buf_fd;
    frame.virtual_address_ = nullptr;
    frame.buffer_size_ = used_size;
    FillFrameLayout(frame, used_size);

    core::FrameDescriptor descriptor;
    descriptor.frame_id = frame_id;
    descriptor.camera_id = frame.camera_id_;
    descriptor.timestamp_ns = frame.timestamp_ns_;
    descriptor.sequence = frame.sequence_;
    descriptor.width = frame.width_;
    descriptor.height = frame.height_;
    descriptor.pixel_format = frame.format_;
    descriptor.memory_type = core::MemoryType::kDmaBuf;
    descriptor.buffer_id = buf.index;
    descriptor.plane_count = frame.plane_count_;
    descriptor.fd_count = 1;
    descriptor.fds[0] = buffer.dma_buf_fd;
    descriptor.total_bytes_used = used_size;
    descriptor.flags = frame.flags_;
    for (uint32_t i = 0; i < frame.plane_count_ && i < core::kMaxFramePlanes; ++i)
    {
        descriptor.planes[i].fd_index = 0;
        descriptor.planes[i].offset = frame.plane_offset_[i];
        descriptor.planes[i].stride = frame.line_stride_[i];
        descriptor.planes[i].length = frame.plane_size_[i];
        descriptor.planes[i].bytes_used = frame.plane_size_[i];
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

                struct v4l2_buffer qbuf;
                std::memset(&qbuf, 0, sizeof(qbuf));
                qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                qbuf.memory = V4L2_MEMORY_MMAP;
                qbuf.index = buffer_index;
                if (Xioctl(context->device_fd, VIDIOC_QBUF, &qbuf) < 0)
                {
                    platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                                  "VIDIOC_QBUF failed: %s", strerror(errno));
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

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "Device does not support V4L2 video capture");
        CloseDevice();
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
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

bool CameraSource::ConfigureDevice()
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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

    platform::PlatformLogger::Log(core::LogLevel::kInfo, "camera_source",
                                  "negotiated format: width=%u height=%u fourcc=%c%c%c%c",
                                  config_.width_,
                                  config_.height_,
                                  fmt.fmt.pix.pixelformat & 0xff,
                                  (fmt.fmt.pix.pixelformat >> 8) & 0xff,
                                  (fmt.fmt.pix.pixelformat >> 16) & 0xff,
                                  (fmt.fmt.pix.pixelformat >> 24) & 0xff);

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        expbuf.index = i;
        expbuf.plane = 0;
        expbuf.flags = O_CLOEXEC;

        if (Xioctl(device_fd_, VIDIOC_EXPBUF, &expbuf) < 0)
        {
            dma_buf_export_failures_.fetch_add(1);
            all_exported = false;
            platform::PlatformLogger::Log(core::LogLevel::kWarning, "camera_source",
                                          "VIDIOC_EXPBUF failed for buffer %u: %s",
                                          i, strerror(errno));
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
                                  buffers_.size(),
                                  min_queued_capture_buffers_,
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

bool CameraSource::ShouldUseDmaBufPath() const
{
    if (!dma_buf_path_enabled_)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(callback_mutex_);
    return static_cast<bool>(frame_packet_callback_);
}

void CameraSource::RequeueBuffer(uint32_t buffer_index)
{
    struct v4l2_buffer qbuf;
    std::memset(&qbuf, 0, sizeof(qbuf));
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = buffer_index;

    if (Xioctl(device_fd_, VIDIOC_QBUF, &qbuf) < 0)
    {
        platform::PlatformLogger::Log(core::LogLevel::kError, "camera_source",
                                      "VIDIOC_QBUF failed: %s", strerror(errno));
    }
}

bool CameraSource::StartStream()
{
    if (streaming_)
    {
        return true;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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
