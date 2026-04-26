#ifndef WEB_PREVIEW_WEB_FRAME_PROTOCOL_H
#define WEB_PREVIEW_WEB_FRAME_PROTOCOL_H

#include <cstdint>

namespace web_preview {

constexpr uint32_t kWebFrameMagic = 0x57504652; // "WPFR"
constexpr uint32_t kWebFrameVersion = 1;

enum class WebPixelFormat : uint32_t
{
    kUnknown = 0,
    kJpeg = 1,
    kRgb = 2,
    kRgba = 3,
    kNv12 = 4,
    kYuyv = 5,
    kUyvy = 6
};

enum WebTransformFlags : uint32_t
{
    kTransformNone = 0,
    kTransformUnsupported = 1U << 0
};

#pragma pack(push, 1)
struct WebFrameHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t stream_id;
    uint64_t frame_id;
    uint64_t timestamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t stride_y;
    uint32_t stride_uv;
    uint32_t payload_size;
    uint32_t transform_flags;
    uint8_t reserved[32];
};
#pragma pack(pop)

} // namespace web_preview

#endif // WEB_PREVIEW_WEB_FRAME_PROTOCOL_H
