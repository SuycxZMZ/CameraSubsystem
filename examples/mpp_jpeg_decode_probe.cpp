/**
 * @file mpp_jpeg_decode_probe.cpp
 * @brief Probe Rockchip MPP MJPEG/JPEG decode path with a single JPEG frame
 *
 * Usage:
 *   ./mpp_jpeg_decode_probe <input.jpg> [nv12|rgba]
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <mpp_task.h>
#include <rk_mpi.h>
#include <rk_type.h>

namespace
{

uint32_t Align16(uint32_t value)
{
    return (value + 15U) & ~15U;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* data)
{
    if (!data)
    {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0)
    {
        return false;
    }
    input.seekg(0, std::ios::beg);

    data->resize(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(data->data()), size);
    return input.good();
}

bool ParseJpegSize(const std::vector<uint8_t>& data, uint32_t* width, uint32_t* height)
{
    if (!width || !height || data.size() < 4 || data[0] != 0xff || data[1] != 0xd8)
    {
        return false;
    }

    size_t pos = 2;
    while (pos + 4 < data.size())
    {
        while (pos < data.size() && data[pos] != 0xff)
        {
            ++pos;
        }
        if (pos + 4 >= data.size())
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
        if (segment_length < 2 || pos + segment_length > data.size())
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
        return "UNKNOWN";
    }
}

MppFrameFormat ParseOutputFormat(const std::string& value)
{
    if (value == "rgba")
    {
        return MPP_FMT_RGBA8888;
    }
    return MPP_FMT_YUV420SP;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Usage: %s <input.jpg> [nv12|rgba]\n", argv[0]);
        return 2;
    }

    const std::string input_path = argv[1];
    const MppFrameFormat requested_format =
        ParseOutputFormat(argc > 2 ? argv[2] : "nv12");

    std::vector<uint8_t> jpeg;
    if (!ReadFile(input_path, &jpeg))
    {
        std::fprintf(stderr, "read_input_failed path=%s\n", input_path.c_str());
        return 1;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!ParseJpegSize(jpeg, &width, &height))
    {
        std::fprintf(stderr, "parse_jpeg_size_failed path=%s size=%zu\n",
                     input_path.c_str(), jpeg.size());
        return 1;
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
    MppFrameFormat output_format = requested_format;
    int ret_code = 1;

    if (mpp_buffer_group_get_internal(&input_group, MPP_BUFFER_TYPE_DRM) != MPP_OK ||
        mpp_buffer_group_get_internal(&frame_group, MPP_BUFFER_TYPE_DRM) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_buffer_group_get_internal_failed\n");
        goto cleanup;
    }

    if (mpp_buffer_get(input_group, &input_buffer, jpeg.size()) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_input_buffer_get_failed\n");
        goto cleanup;
    }
    std::memcpy(mpp_buffer_get_ptr(input_buffer), jpeg.data(), jpeg.size());

    if (mpp_packet_init_with_buffer(&packet, input_buffer) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_packet_init_with_buffer_failed\n");
        goto cleanup;
    }
    mpp_packet_set_size(packet, jpeg.size());
    mpp_packet_set_length(packet, jpeg.size());

    if (mpp_buffer_get(frame_group, &frame_buffer, output_buffer_size) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_frame_buffer_get_failed size=%zu\n",
                     output_buffer_size);
        goto cleanup;
    }

    if (mpp_frame_init(&frame) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_frame_init_failed\n");
        goto cleanup;
    }
    mpp_frame_set_buffer(frame, frame_buffer);

    if (mpp_create(&ctx, &mpi) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_create_failed\n");
        goto cleanup;
    }
    if (mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_init_mjpeg_failed\n");
        goto cleanup;
    }

    if (mpi->control(ctx, MPP_DEC_SET_OUTPUT_FORMAT, &output_format) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_set_output_format_failed format=%d\n",
                     static_cast<int>(requested_format));
        goto cleanup;
    }

    if (mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK) != MPP_OK ||
        mpi->dequeue(ctx, MPP_PORT_INPUT, &task) != MPP_OK || !task)
    {
        std::fprintf(stderr, "mpp_input_task_dequeue_failed\n");
        goto cleanup;
    }

    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);

    if (mpi->enqueue(ctx, MPP_PORT_INPUT, task) != MPP_OK)
    {
        std::fprintf(stderr, "mpp_input_task_enqueue_failed\n");
        task = nullptr;
        goto cleanup;
    }
    task = nullptr;

    if (mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK) != MPP_OK ||
        mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task) != MPP_OK || !task)
    {
        std::fprintf(stderr, "mpp_output_task_dequeue_failed\n");
        goto cleanup;
    }

    mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &output_frame);
    if (!output_frame)
    {
        std::fprintf(stderr, "mpp_output_frame_missing\n");
        goto cleanup;
    }

    std::printf("mpp_jpeg_decode_probe_input=%s\n", input_path.c_str());
    std::printf("jpeg_size=%zu\n", jpeg.size());
    std::printf("jpeg_width=%u\n", width);
    std::printf("jpeg_height=%u\n", height);
    std::printf("requested_output_format=%s\n", FormatName(requested_format));
    std::printf("frame_width=%u\n", mpp_frame_get_width(output_frame));
    std::printf("frame_height=%u\n", mpp_frame_get_height(output_frame));
    std::printf("frame_hor_stride=%u\n", mpp_frame_get_hor_stride(output_frame));
    std::printf("frame_ver_stride=%u\n", mpp_frame_get_ver_stride(output_frame));
    std::printf("frame_format=%s\n", FormatName(mpp_frame_get_fmt(output_frame)));
    std::printf("frame_format_raw=%d\n", static_cast<int>(mpp_frame_get_fmt(output_frame)));
    std::printf("frame_buf_size=%zu\n", mpp_frame_get_buf_size(output_frame));
    std::printf("frame_errinfo=%u\n", mpp_frame_get_errinfo(output_frame));
    std::printf("frame_discard=%u\n", mpp_frame_get_discard(output_frame));

    if (mpp_frame_get_errinfo(output_frame) == 0 &&
        mpp_frame_get_discard(output_frame) == 0 &&
        mpp_frame_get_width(output_frame) > 0 &&
        mpp_frame_get_height(output_frame) > 0)
    {
        std::printf("mpp_jpeg_decode_probe_result=PASS\n");
        ret_code = 0;
    }
    else
    {
        std::printf("mpp_jpeg_decode_probe_result=FAIL\n");
    }

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

    return ret_code;
}
