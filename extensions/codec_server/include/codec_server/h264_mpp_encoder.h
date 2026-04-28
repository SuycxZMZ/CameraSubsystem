#ifndef CODEC_SERVER_H264_MPP_ENCODER_H
#define CODEC_SERVER_H264_MPP_ENCODER_H

#include "codec_server/jpeg_decode_stage.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace camera_subsystem::extensions::codec_server {

enum class H264EncodeResult
{
    kOk,
    kEncoderNotAvailable,
    kInvalidInput,
    kInitFailed,
    kEncodeFailed,
};

struct H264EncoderConfig
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t hor_stride = 0;
    uint32_t ver_stride = 0;
    uint32_t fps = 30;
    uint32_t bitrate = 4000000;
    uint32_t gop = 60;
};

struct EncodedPacket
{
    std::vector<uint8_t> payload;
};

class H264MppEncoder
{
public:
    H264MppEncoder();
    ~H264MppEncoder();

    H264MppEncoder(const H264MppEncoder&) = delete;
    H264MppEncoder& operator=(const H264MppEncoder&) = delete;

    bool IsAvailable() const;
    bool IsOpen() const;
    H264EncodeResult Open(const H264EncoderConfig& config);
    H264EncodeResult EncodeFrame(const DecodedImageFrame& frame,
                                 std::vector<EncodedPacket>* packets);
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* ToErrorString(H264EncodeResult result);

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_H264_MPP_ENCODER_H
