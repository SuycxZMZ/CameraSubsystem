#include "codec_server/jpeg_decode_stage.h"

#include <cstring>

#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <mpp_task.h>
#include <rk_mpi.h>
#include <rk_type.h>
#endif

namespace camera_subsystem::extensions::codec_server {
namespace {

#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
uint32_t Align16(uint32_t value)
{
    return (value + 15U) & ~15U;
}

bool ParseJpegSize(const uint8_t* data, size_t size, uint32_t* width, uint32_t* height)
{
    if (!data || !width || !height || size < 4 || data[0] != 0xff || data[1] != 0xd8)
    {
        return false;
    }

    size_t pos = 2;
    while (pos + 4 < size)
    {
        while (pos < size && data[pos] != 0xff)
        {
            ++pos;
        }
        if (pos + 4 >= size)
        {
            return false;
        }

        const uint8_t marker = data[pos + 1];
        pos += 2;

        if (marker == 0xd8 || marker == 0xd9)
        {
            continue;
        }
        if (marker >= 0xd0 && marker <= 0xd7)
        {
            continue;
        }

        const uint16_t segment_length =
            static_cast<uint16_t>((data[pos] << 8U) | data[pos + 1]);
        if (segment_length < 2 || pos + segment_length > size)
        {
            return false;
        }

        const bool is_sof =
            (marker >= 0xc0 && marker <= 0xc3) ||
            (marker >= 0xc5 && marker <= 0xc7) ||
            (marker >= 0xc9 && marker <= 0xcb) ||
            (marker >= 0xcd && marker <= 0xcf);
        if (is_sof)
        {
            if (segment_length < 7)
            {
                return false;
            }
            *height = static_cast<uint32_t>((data[pos + 3] << 8U) | data[pos + 4]);
            *width = static_cast<uint32_t>((data[pos + 5] << 8U) | data[pos + 6]);
            return *width != 0 && *height != 0;
        }

        pos += segment_length;
    }

    return false;
}

const char* FormatName(MppFrameFormat format)
{
    switch (format & MPP_FRAME_FMT_MASK)
    {
    case MPP_FMT_YUV420SP:
        return "NV12";
    case MPP_FMT_YUV420SP_VU:
        return "NV21";
    case MPP_FMT_RGBA8888:
        return "RGBA";
    case MPP_FMT_BGRA8888:
        return "BGRA";
    default:
        return "unknown";
    }
}
#endif

} // namespace

bool JpegDecodeStage::IsAvailable() const
{
#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
    return true;
#else
    return false;
#endif
}

JpegDecodeResult JpegDecodeStage::Decode(const uint8_t* data,
                                         size_t size,
                                         DecodedImageFrame* output) const
{
    if (!data || size == 0 || !output)
    {
        return JpegDecodeResult::kInvalidInput;
    }

#ifndef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
    (void)data;
    (void)size;
    (void)output;
    return JpegDecodeResult::kDecoderNotAvailable;
#else
    uint32_t width = 0;
    uint32_t height = 0;
    if (!ParseJpegSize(data, size, &width, &height))
    {
        return JpegDecodeResult::kInvalidInput;
    }

    const uint32_t hor_stride = Align16(width);
    const uint32_t ver_stride = Align16(height);
    const size_t output_buffer_size =
        static_cast<size_t>(hor_stride) * static_cast<size_t>(ver_stride) * 4U;

    MppCtx ctx = nullptr;
    MppApi* mpi = nullptr;
    MppBufferGroup input_group = nullptr;
    MppBufferGroup frame_group = nullptr;
    MppBuffer input_buffer = nullptr;
    MppBuffer frame_buffer = nullptr;
    MppPacket packet = nullptr;
    MppFrame frame = nullptr;
    MppFrame output_frame = nullptr;
    MppTask task = nullptr;
    MppFrameFormat output_format = MPP_FMT_YUV420SP;
    JpegDecodeResult result = JpegDecodeResult::kDecodeFailed;

    if (mpp_buffer_group_get_internal(&input_group, MPP_BUFFER_TYPE_DRM) != MPP_OK ||
        mpp_buffer_group_get_internal(&frame_group, MPP_BUFFER_TYPE_DRM) != MPP_OK)
    {
        goto cleanup;
    }

    if (mpp_buffer_get(input_group, &input_buffer, size) != MPP_OK)
    {
        goto cleanup;
    }
    std::memcpy(mpp_buffer_get_ptr(input_buffer), data, size);

    if (mpp_packet_init_with_buffer(&packet, input_buffer) != MPP_OK)
    {
        goto cleanup;
    }
    mpp_packet_set_size(packet, size);
    mpp_packet_set_length(packet, size);

    if (mpp_buffer_get(frame_group, &frame_buffer, output_buffer_size) != MPP_OK)
    {
        goto cleanup;
    }

    if (mpp_frame_init(&frame) != MPP_OK)
    {
        goto cleanup;
    }
    mpp_frame_set_buffer(frame, frame_buffer);

    if (mpp_create(&ctx, &mpi) != MPP_OK)
    {
        goto cleanup;
    }
    if (mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) != MPP_OK)
    {
        goto cleanup;
    }

    if (mpi->control(ctx, MPP_DEC_SET_OUTPUT_FORMAT, &output_format) != MPP_OK)
    {
        goto cleanup;
    }

    if (mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK) != MPP_OK ||
        mpi->dequeue(ctx, MPP_PORT_INPUT, &task) != MPP_OK || !task)
    {
        goto cleanup;
    }

    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);

    if (mpi->enqueue(ctx, MPP_PORT_INPUT, task) != MPP_OK)
    {
        task = nullptr;
        goto cleanup;
    }
    task = nullptr;

    if (mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK) != MPP_OK ||
        mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task) != MPP_OK || !task)
    {
        goto cleanup;
    }

    mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &output_frame);
    if (!output_frame ||
        mpp_frame_get_errinfo(output_frame) != 0 ||
        mpp_frame_get_discard(output_frame) != 0 ||
        mpp_frame_get_width(output_frame) == 0 ||
        mpp_frame_get_height(output_frame) == 0)
    {
        goto cleanup;
    }

    {
        MppBuffer output_buffer = mpp_frame_get_buffer(output_frame);
        void* output_ptr = output_buffer ? mpp_buffer_get_ptr(output_buffer) : nullptr;
        const size_t frame_size = mpp_frame_get_buf_size(output_frame);
        if (!output_ptr || frame_size == 0)
        {
            goto cleanup;
        }

        output->width = mpp_frame_get_width(output_frame);
        output->height = mpp_frame_get_height(output_frame);
        output->hor_stride = mpp_frame_get_hor_stride(output_frame);
        output->ver_stride = mpp_frame_get_ver_stride(output_frame);
        output->pixel_format = FormatName(mpp_frame_get_fmt(output_frame));
        output->payload.resize(frame_size);
        std::memcpy(output->payload.data(), output_ptr, frame_size);
    }
    result = JpegDecodeResult::kOk;

cleanup:
    if (ctx && mpi && task)
    {
        (void)mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);
    }
    if (packet)
    {
        mpp_packet_deinit(&packet);
    }
    if (frame)
    {
        mpp_frame_deinit(&frame);
    }
    if (frame_buffer)
    {
        mpp_buffer_put(frame_buffer);
    }
    if (input_buffer)
    {
        mpp_buffer_put(input_buffer);
    }
    if (frame_group)
    {
        mpp_buffer_group_put(frame_group);
    }
    if (input_group)
    {
        mpp_buffer_group_put(input_group);
    }
    if (ctx)
    {
        mpp_destroy(ctx);
    }

    return result;
#endif
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
