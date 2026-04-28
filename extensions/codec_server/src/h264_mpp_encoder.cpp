#include "codec_server/h264_mpp_encoder.h"

#include <algorithm>
#include <cstring>

#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_mpi.h>
#include <rk_type.h>
#include <rk_venc_cfg.h>
#include <rk_venc_cmd.h>
#include <rk_venc_rc.h>
#endif

namespace camera_subsystem::extensions::codec_server {
namespace {

#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
uint32_t Align64(uint32_t value)
{
    return (value + 63U) & ~63U;
}

size_t Nv12FrameSize(uint32_t hor_stride, uint32_t ver_stride)
{
    return static_cast<size_t>(Align64(hor_stride)) *
           static_cast<size_t>(Align64(ver_stride)) * 3U / 2U;
}
#endif

} // namespace

struct H264MppEncoder::Impl
{
#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
    MppCtx ctx = nullptr;
    MppApi* mpi = nullptr;
    MppEncCfg cfg = nullptr;
    MppBufferGroup buffer_group = nullptr;
    H264EncoderConfig config;
    bool is_open = false;
    bool header_sent = false;
#else
    bool is_open = false;
#endif
};

H264MppEncoder::H264MppEncoder()
    : impl_(new Impl())
{
}

H264MppEncoder::~H264MppEncoder()
{
    Close();
}

bool H264MppEncoder::IsAvailable() const
{
#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
    return true;
#else
    return false;
#endif
}

bool H264MppEncoder::IsOpen() const
{
    return impl_->is_open;
}

H264EncodeResult H264MppEncoder::Open(const H264EncoderConfig& config)
{
    Close();

    if (config.width == 0 || config.height == 0 ||
        config.hor_stride == 0 || config.ver_stride == 0)
    {
        return H264EncodeResult::kInvalidInput;
    }

#ifndef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
    (void)config;
    return H264EncodeResult::kEncoderNotAvailable;
#else
    H264EncodeResult result = H264EncodeResult::kInitFailed;
    MppPollType timeout = MPP_POLL_BLOCK;

    if (mpp_create(&impl_->ctx, &impl_->mpi) != MPP_OK)
    {
        goto fail;
    }
    if (impl_->mpi->control(impl_->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout) != MPP_OK)
    {
        goto fail;
    }
    if (mpp_init(impl_->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC) != MPP_OK)
    {
        goto fail;
    }
    if (mpp_enc_cfg_init(&impl_->cfg) != MPP_OK)
    {
        goto fail;
    }
    if (impl_->mpi->control(impl_->ctx, MPP_ENC_GET_CFG, impl_->cfg) != MPP_OK)
    {
        goto fail;
    }
    if (mpp_buffer_group_get_internal(&impl_->buffer_group,
                                      MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK)
    {
        goto fail;
    }

    mpp_enc_cfg_set_s32(impl_->cfg, "prep:width", config.width);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:height", config.height);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:hor_stride", config.hor_stride);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:ver_stride", config.ver_stride);
    mpp_enc_cfg_set_s32(impl_->cfg, "prep:format", MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(impl_->cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_in_num", config.fps == 0 ? 30 : config.fps);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_in_denom", 1);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_out_num", config.fps == 0 ? 30 : config.fps);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_u32(impl_->cfg, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    mpp_enc_cfg_set_u32(impl_->cfg, "rc:drop_thd", 20);
    mpp_enc_cfg_set_u32(impl_->cfg, "rc:drop_gap", 1);

    mpp_enc_cfg_set_s32(impl_->cfg, "rc:bps_target",
                        config.bitrate == 0 ? 4000000 : config.bitrate);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:bps_max",
                        static_cast<int>((config.bitrate == 0 ? 4000000 : config.bitrate) * 17U / 16U));
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:bps_min",
                        static_cast<int>((config.bitrate == 0 ? 4000000 : config.bitrate) * 15U / 16U));
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:qp_init", -1);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:qp_max", 51);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:qp_min", 10);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:qp_max_i", 51);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:qp_min_i", 10);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:qp_ip", 2);
    mpp_enc_cfg_set_s32(impl_->cfg, "rc:gop", config.gop == 0 ? 60 : config.gop);

    mpp_enc_cfg_set_s32(impl_->cfg, "codec:type", MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(impl_->cfg, "h264:profile", 100);
    mpp_enc_cfg_set_s32(impl_->cfg, "h264:level", 40);
    mpp_enc_cfg_set_s32(impl_->cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(impl_->cfg, "h264:cabac_idc", 0);
    mpp_enc_cfg_set_s32(impl_->cfg, "h264:trans8x8", 1);

    if (impl_->mpi->control(impl_->ctx, MPP_ENC_SET_CFG, impl_->cfg) != MPP_OK)
    {
        goto fail;
    }
    {
        MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
        if (impl_->mpi->control(impl_->ctx, MPP_ENC_SET_HEADER_MODE, &header_mode) != MPP_OK)
        {
            goto fail;
        }
    }
    {
        MppEncSeiMode sei_mode = MPP_ENC_SEI_MODE_ONE_FRAME;
        if (impl_->mpi->control(impl_->ctx, MPP_ENC_SET_SEI_CFG, &sei_mode) != MPP_OK)
        {
            goto fail;
        }
    }

    impl_->config = config;
    impl_->is_open = true;
    impl_->header_sent = false;
    return H264EncodeResult::kOk;

fail:
    Close();
    return result;
#endif
}

H264EncodeResult H264MppEncoder::EncodeFrame(const DecodedImageFrame& frame,
                                             std::vector<EncodedPacket>* packets)
{
    if (!packets)
    {
        return H264EncodeResult::kInvalidInput;
    }
    packets->clear();

    if (!impl_->is_open)
    {
        return H264EncodeResult::kInitFailed;
    }
    if (frame.width == 0 || frame.height == 0 ||
        frame.hor_stride == 0 || frame.ver_stride == 0 ||
        frame.pixel_format != "NV12" || frame.payload.empty())
    {
        return H264EncodeResult::kInvalidInput;
    }

#ifndef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
    (void)frame;
    return H264EncodeResult::kEncoderNotAvailable;
#else
    if (!impl_->header_sent)
    {
        MppBuffer header_buffer = nullptr;
        MppPacket header_packet = nullptr;
        const size_t packet_buffer_size =
            std::max(Nv12FrameSize(frame.hor_stride, frame.ver_stride), frame.payload.size());
        if (mpp_buffer_get(impl_->buffer_group, &header_buffer, packet_buffer_size) != MPP_OK)
        {
            return H264EncodeResult::kEncodeFailed;
        }
        if (mpp_packet_init_with_buffer(&header_packet, header_buffer) != MPP_OK)
        {
            mpp_buffer_put(header_buffer);
            return H264EncodeResult::kEncodeFailed;
        }
        mpp_packet_set_length(header_packet, 0);
        if (impl_->mpi->control(impl_->ctx, MPP_ENC_GET_HDR_SYNC, header_packet) != MPP_OK)
        {
            mpp_packet_deinit(&header_packet);
            mpp_buffer_put(header_buffer);
            return H264EncodeResult::kEncodeFailed;
        }
        const size_t header_size = mpp_packet_get_length(header_packet);
        if (header_size > 0)
        {
            const auto* ptr = static_cast<const uint8_t*>(mpp_packet_get_pos(header_packet));
            EncodedPacket packet;
            packet.payload.assign(ptr, ptr + header_size);
            packets->push_back(std::move(packet));
        }
        mpp_packet_deinit(&header_packet);
        mpp_buffer_put(header_buffer);
        impl_->header_sent = true;
    }

    MppBuffer frame_buffer = nullptr;
    MppFrame mpp_frame = nullptr;
    MppPacket packet = nullptr;
    H264EncodeResult result = H264EncodeResult::kEncodeFailed;
    const size_t frame_buffer_size =
        std::max(Nv12FrameSize(frame.hor_stride, frame.ver_stride), frame.payload.size());

    if (mpp_buffer_get(impl_->buffer_group, &frame_buffer, frame_buffer_size) != MPP_OK)
    {
        goto cleanup;
    }
    mpp_buffer_sync_begin(frame_buffer);
    std::memcpy(mpp_buffer_get_ptr(frame_buffer), frame.payload.data(), frame.payload.size());
    mpp_buffer_sync_end(frame_buffer);

    if (mpp_frame_init(&mpp_frame) != MPP_OK)
    {
        goto cleanup;
    }
    mpp_frame_set_width(mpp_frame, frame.width);
    mpp_frame_set_height(mpp_frame, frame.height);
    mpp_frame_set_hor_stride(mpp_frame, frame.hor_stride);
    mpp_frame_set_ver_stride(mpp_frame, frame.ver_stride);
    mpp_frame_set_fmt(mpp_frame, MPP_FMT_YUV420SP);
    mpp_frame_set_eos(mpp_frame, 0);
    mpp_frame_set_buffer(mpp_frame, frame_buffer);

    if (impl_->mpi->encode_put_frame(impl_->ctx, mpp_frame) != MPP_OK)
    {
        goto cleanup;
    }
    mpp_frame_deinit(&mpp_frame);

    if (impl_->mpi->encode_get_packet(impl_->ctx, &packet) != MPP_OK || !packet)
    {
        goto cleanup;
    }
    {
        const size_t packet_size = mpp_packet_get_length(packet);
        if (packet_size > 0)
        {
            const auto* ptr = static_cast<const uint8_t*>(mpp_packet_get_pos(packet));
            EncodedPacket out;
            out.payload.assign(ptr, ptr + packet_size);
            packets->push_back(std::move(out));
        }
    }
    result = H264EncodeResult::kOk;

cleanup:
    if (packet)
    {
        mpp_packet_deinit(&packet);
    }
    if (mpp_frame)
    {
        mpp_frame_deinit(&mpp_frame);
    }
    if (frame_buffer)
    {
        mpp_buffer_put(frame_buffer);
    }
    return result;
#endif
}

void H264MppEncoder::Close()
{
#ifdef CODEC_SERVER_ENABLE_MPP_JPEG_DECODE
    if (impl_->cfg)
    {
        mpp_enc_cfg_deinit(impl_->cfg);
        impl_->cfg = nullptr;
    }
    if (impl_->buffer_group)
    {
        mpp_buffer_group_put(impl_->buffer_group);
        impl_->buffer_group = nullptr;
    }
    if (impl_->ctx)
    {
        mpp_destroy(impl_->ctx);
        impl_->ctx = nullptr;
        impl_->mpi = nullptr;
    }
    impl_->header_sent = false;
#endif
    impl_->is_open = false;
}

const char* ToErrorString(H264EncodeResult result)
{
    switch (result)
    {
    case H264EncodeResult::kOk:
        return "";
    case H264EncodeResult::kEncoderNotAvailable:
        return "encoder_not_available";
    case H264EncodeResult::kInvalidInput:
        return "invalid_encoder_input";
    case H264EncodeResult::kInitFailed:
        return "encoder_init_failed";
    case H264EncodeResult::kEncodeFailed:
        return "encoder_encode_failed";
    }
    return "encoder_encode_failed";
}

} // namespace camera_subsystem::extensions::codec_server
