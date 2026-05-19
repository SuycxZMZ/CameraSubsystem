#include "detection_server/frame_preprocessor.h"

#include "camera_subsystem/core/types.h"
#include "codec_server/jpeg_decode_stage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__linux__)
#include <linux/videodev2.h>
#endif

namespace camera_subsystem::extensions::detection_server {
namespace {

uint8_t ClampToByte(int value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > 255)
    {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

void Nv12PixelToRgb(const uint8_t* y_plane, const uint8_t* uv_plane, uint32_t hor_stride,
                    uint32_t x, uint32_t y, uint8_t* rgb_out)
{
    const uint32_t uv_x = (x / 2U) * 2U;
    const uint32_t uv_y = y / 2U;

    const int y_value = static_cast<int>(y_plane[y * hor_stride + x]);
    const int u_value = static_cast<int>(uv_plane[uv_y * hor_stride + uv_x]) - 128;
    const int v_value = static_cast<int>(uv_plane[uv_y * hor_stride + uv_x + 1U]) - 128;

    const int c = std::max(0, y_value - 16);
    const int d = u_value;
    const int e = v_value;

    const int r = (298 * c + 409 * e + 128) >> 8;
    const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    const int b = (298 * c + 516 * d + 128) >> 8;

    rgb_out[0] = ClampToByte(r);
    rgb_out[1] = ClampToByte(g);
    rgb_out[2] = ClampToByte(b);
}

bool ConvertNv12ToLetterboxedRgb(const camera_subsystem::extensions::codec_server::DecodedImageFrame& decoded,
                                 const RknnRuntimeInfo& runtime_info,
                                 PreprocessedFrame* output)
{
    if (!output || runtime_info.model_input_width == 0 || runtime_info.model_input_height == 0 ||
        runtime_info.model_input_channels != 3 || decoded.width == 0 || decoded.height == 0 ||
        decoded.hor_stride == 0 || decoded.ver_stride == 0)
    {
        return false;
    }
    if (decoded.pixel_format != "NV12")
    {
        return false;
    }

    const size_t required_nv12_size =
        static_cast<size_t>(decoded.hor_stride) * decoded.ver_stride * 3U / 2U;
    if (decoded.payload.size() < required_nv12_size)
    {
        return false;
    }

    output->input_tensor.width = runtime_info.model_input_width;
    output->input_tensor.height = runtime_info.model_input_height;
    output->input_tensor.channels = runtime_info.model_input_channels;
    output->input_tensor.bytes.assign(
        static_cast<size_t>(runtime_info.model_input_width) * runtime_info.model_input_height * 3U,
        114U);

    output->letterbox.source_width = decoded.width;
    output->letterbox.source_height = decoded.height;
    output->letterbox.model_input_width = runtime_info.model_input_width;
    output->letterbox.model_input_height = runtime_info.model_input_height;

    const float scale_x = static_cast<float>(runtime_info.model_input_width) /
                          static_cast<float>(decoded.width);
    const float scale_y = static_cast<float>(runtime_info.model_input_height) /
                          static_cast<float>(decoded.height);
    const float scale = std::min(scale_x, scale_y);
    output->letterbox.scale = scale;

    const uint32_t resized_width =
        std::max(1U, static_cast<uint32_t>(std::lround(decoded.width * scale)));
    const uint32_t resized_height =
        std::max(1U, static_cast<uint32_t>(std::lround(decoded.height * scale)));

    output->letterbox.x_pad =
        static_cast<float>(runtime_info.model_input_width - resized_width) * 0.5f;
    output->letterbox.y_pad =
        static_cast<float>(runtime_info.model_input_height - resized_height) * 0.5f;

    const uint8_t* y_plane = decoded.payload.data();
    const uint8_t* uv_plane =
        decoded.payload.data() + static_cast<size_t>(decoded.hor_stride) * decoded.ver_stride;

    for (uint32_t dst_y = 0; dst_y < resized_height; ++dst_y)
    {
        const uint32_t src_y = std::min(
            decoded.height - 1U,
            static_cast<uint32_t>(std::floor(static_cast<float>(dst_y) / scale)));
        const uint32_t letterbox_y =
            static_cast<uint32_t>(output->letterbox.y_pad) + dst_y;
        for (uint32_t dst_x = 0; dst_x < resized_width; ++dst_x)
        {
            const uint32_t src_x = std::min(
                decoded.width - 1U,
                static_cast<uint32_t>(std::floor(static_cast<float>(dst_x) / scale)));
            const uint32_t letterbox_x =
                static_cast<uint32_t>(output->letterbox.x_pad) + dst_x;
            uint8_t* rgb = &output->input_tensor.bytes
                                [(static_cast<size_t>(letterbox_y) * runtime_info.model_input_width +
                                  letterbox_x) *
                                 3U];
            Nv12PixelToRgb(y_plane, uv_plane, decoded.hor_stride, src_x, src_y, rgb);
        }
    }

    return true;
}

} // namespace

bool MjpegFramePreprocessor::SupportsFrame(uint32_t pixel_format) const
{
    if (pixel_format ==
        static_cast<uint32_t>(camera_subsystem::core::PixelFormat::kMJPEG))
    {
        return true;
    }
#if defined(V4L2_PIX_FMT_MJPEG)
    if (pixel_format == V4L2_PIX_FMT_MJPEG)
    {
        return true;
    }
#endif
#if defined(V4L2_PIX_FMT_JPEG)
    if (pixel_format == V4L2_PIX_FMT_JPEG)
    {
        return true;
    }
#endif
    return false;
}

bool MjpegFramePreprocessor::Preprocess(const camera_subsystem::ipc::CameraDataFrameHeader& header,
                                        const std::vector<uint8_t>& payload,
                                        const RknnRuntimeInfo& runtime_info,
                                        PreprocessedFrame* output,
                                        std::string* error_message) const
{
    if (!output)
    {
        if (error_message)
        {
            *error_message = "preprocess output must not be null";
        }
        return false;
    }
    if (!SupportsFrame(header.pixel_format))
    {
        if (error_message)
        {
            *error_message = "unsupported pixel format for mjpeg preprocessor";
        }
        return false;
    }

    camera_subsystem::extensions::codec_server::JpegDecodeStage decoder;
    camera_subsystem::extensions::codec_server::DecodedImageFrame decoded;
    const auto decode_result = decoder.Decode(payload.data(), payload.size(), &decoded);
    if (decode_result != camera_subsystem::extensions::codec_server::JpegDecodeResult::kOk)
    {
        if (error_message)
        {
            *error_message =
                camera_subsystem::extensions::codec_server::ToErrorString(decode_result);
        }
        return false;
    }

    if (!ConvertNv12ToLetterboxedRgb(decoded, runtime_info, output))
    {
        if (error_message)
        {
            *error_message = "failed to convert decoded nv12 frame to model rgb input";
        }
        return false;
    }

    if (error_message)
    {
        error_message->clear();
    }
    return true;
}

} // namespace camera_subsystem::extensions::detection_server
