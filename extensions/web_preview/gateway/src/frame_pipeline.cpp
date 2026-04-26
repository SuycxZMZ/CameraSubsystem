#include "web_preview/frame_pipeline.h"

#include <cstring>

#include "camera_subsystem/core/types.h"

namespace web_preview {

FramePipeline::FramePipeline()
    : max_fps_(15)
    , last_publish_time_(std::chrono::steady_clock::time_point::min())
{
}

void FramePipeline::SetMaxFps(uint32_t max_fps)
{
    std::lock_guard<std::mutex> lock(mutex_);
    max_fps_ = max_fps == 0 ? 1 : max_fps;
}

void FramePipeline::SetPacketCallback(PacketCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    packet_callback_ = std::move(callback);
}

void FramePipeline::SetStatusCallback(StatusCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_callback_ = std::move(callback);
}

void FramePipeline::SubmitFrame(CameraFrame&& frame)
{
    PacketCallback packet_callback;
    StatusCallback status_callback;
    std::vector<uint8_t> packet;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.input_frames;

        const WebPixelFormat web_format = MapPixelFormat(frame.header.pixel_format);
        stats_.width = frame.header.width;
        stats_.height = frame.header.height;
        stats_.pixel_format = web_format;

        if (web_format != WebPixelFormat::kJpeg)
        {
            ++stats_.unsupported_frames;
            ++stats_.dropped_frames;
            stats_.status = "unsupported_pixel_format";
            status_callback = status_callback_;
        }
        else if (!ShouldPublishNow())
        {
            ++stats_.dropped_frames;
            stats_.status = "rate_limited";
            status_callback = status_callback_;
        }
        else
        {
            WebFrameHeader web_header;
            std::memset(&web_header, 0, sizeof(web_header));
            web_header.magic = kWebFrameMagic;
            web_header.version = kWebFrameVersion;
            web_header.header_size = sizeof(WebFrameHeader);
            web_header.stream_id = 0;
            web_header.frame_id = frame.header.frame_id;
            web_header.timestamp_ns = frame.header.timestamp_ns;
            web_header.width = frame.header.width;
            web_header.height = frame.header.height;
            web_header.pixel_format = static_cast<uint32_t>(web_format);
            web_header.payload_size = frame.header.frame_size;
            web_header.transform_flags = kTransformNone;

            packet.resize(sizeof(web_header) + frame.payload.size());
            std::memcpy(packet.data(), &web_header, sizeof(web_header));
            std::memcpy(packet.data() + sizeof(web_header), frame.payload.data(),
                        frame.payload.size());

            ++stats_.published_frames;
            stats_.status = "streaming";
            last_publish_time_ = std::chrono::steady_clock::now();
            packet_callback = packet_callback_;
            status_callback = status_callback_;
        }
    }

    if (status_callback)
    {
        status_callback(GetStats());
    }
    if (packet_callback && !packet.empty())
    {
        packet_callback(packet);
    }
}

StreamStats FramePipeline::GetStats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

WebPixelFormat FramePipeline::MapPixelFormat(uint32_t camera_pixel_format) const
{
    using camera_subsystem::core::PixelFormat;
    switch (static_cast<PixelFormat>(camera_pixel_format))
    {
        case PixelFormat::kMJPEG:
            return WebPixelFormat::kJpeg;
        case PixelFormat::kRGB888:
            return WebPixelFormat::kRgb;
        case PixelFormat::kRGBA8888:
            return WebPixelFormat::kRgba;
        case PixelFormat::kNV12:
            return WebPixelFormat::kNv12;
        case PixelFormat::kYUYV:
            return WebPixelFormat::kYuyv;
        default:
            return WebPixelFormat::kUnknown;
    }
}

bool FramePipeline::ShouldPublishNow()
{
    const auto now = std::chrono::steady_clock::now();
    if (last_publish_time_ == std::chrono::steady_clock::time_point::min())
    {
        return true;
    }
    const auto min_interval = std::chrono::milliseconds(1000 / (max_fps_ == 0 ? 1 : max_fps_));
    return now - last_publish_time_ >= min_interval;
}

} // namespace web_preview
