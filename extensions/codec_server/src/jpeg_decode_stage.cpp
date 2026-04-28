#include "codec_server/jpeg_decode_stage.h"

namespace camera_subsystem::extensions::codec_server {

bool JpegDecodeStage::IsAvailable() const
{
    return false;
}

JpegDecodeResult JpegDecodeStage::Decode(const uint8_t* data,
                                         size_t size,
                                         DecodedImageFrame* output) const
{
    if (!data || size == 0 || !output)
    {
        return JpegDecodeResult::kInvalidInput;
    }

    return JpegDecodeResult::kDecoderNotAvailable;
}

const char* ToErrorString(JpegDecodeResult result)
{
    switch (result)
    {
    case JpegDecodeResult::kOk:
        return "";
    case JpegDecodeResult::kDecoderNotAvailable:
        return "jpeg_decoder_not_available";
    case JpegDecodeResult::kInvalidInput:
        return "invalid_jpeg_input";
    case JpegDecodeResult::kDecodeFailed:
        return "jpeg_decode_failed";
    }
    return "jpeg_decode_failed";
}

} // namespace camera_subsystem::extensions::codec_server
