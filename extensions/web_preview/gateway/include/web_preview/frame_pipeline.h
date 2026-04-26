#ifndef WEB_PREVIEW_FRAME_PIPELINE_H
#define WEB_PREVIEW_FRAME_PIPELINE_H

#include "web_preview/camera_subscriber_client.h"
#include "web_preview/web_frame_protocol.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace web_preview {

struct StreamStats
{
    uint64_t input_frames = 0;
    uint64_t published_frames = 0;
    uint64_t dropped_frames = 0;
    uint64_t unsupported_frames = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    WebPixelFormat pixel_format = WebPixelFormat::kUnknown;
    std::string status = "idle";
};

class FramePipeline
{
public:
    using PacketCallback = std::function<void(const std::vector<uint8_t>&)>;
    using StatusCallback = std::function<void(const StreamStats&)>;

    FramePipeline();

    void SetMaxFps(uint32_t max_fps);
    void SetPacketCallback(PacketCallback callback);
    void SetStatusCallback(StatusCallback callback);
    void SubmitFrame(CameraFrame&& frame);
    StreamStats GetStats() const;

private:
    WebPixelFormat MapPixelFormat(uint32_t camera_pixel_format) const;
    bool ShouldPublishNow();
    void NotifyStatus();

    uint32_t max_fps_;
    mutable std::mutex mutex_;
    StreamStats stats_;
    std::chrono::steady_clock::time_point last_publish_time_;
    PacketCallback packet_callback_;
    StatusCallback status_callback_;
};

} // namespace web_preview

#endif // WEB_PREVIEW_FRAME_PIPELINE_H
