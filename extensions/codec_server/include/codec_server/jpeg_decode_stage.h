#ifndef CODEC_SERVER_JPEG_DECODE_STAGE_H
#define CODEC_SERVER_JPEG_DECODE_STAGE_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace camera_subsystem::extensions::codec_server {

enum class JpegDecodeResult
{
    kOk,
    kDecoderNotAvailable,
    kInvalidInput,
    kDecodeFailed,
};

struct DecodedImageFrame
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t hor_stride = 0;
    uint32_t ver_stride = 0;
    std::string pixel_format = "unknown";
    std::vector<uint8_t> payload;
};

class JpegDecodeStage
{
public:
    bool IsAvailable() const;
    JpegDecodeResult Decode(const uint8_t* data,
                            size_t size,
                            DecodedImageFrame* output) const;
};

const char* ToErrorString(JpegDecodeResult result);

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_JPEG_DECODE_STAGE_H
