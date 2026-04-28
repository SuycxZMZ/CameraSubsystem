#include "codec_server/h264_mpp_encoder.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using camera_subsystem::extensions::codec_server::DecodedImageFrame;
using camera_subsystem::extensions::codec_server::EncodedPacket;
using camera_subsystem::extensions::codec_server::H264EncodeResult;
using camera_subsystem::extensions::codec_server::H264EncoderConfig;
using camera_subsystem::extensions::codec_server::H264MppEncoder;
using camera_subsystem::extensions::codec_server::ToErrorString;

static int g_pass = 0;
static int g_fail = 0;

static void Report(const char* name, bool condition)
{
    if (condition)
    {
        ++g_pass;
        std::cout << "  PASS: " << name << "\n";
    }
    else
    {
        ++g_fail;
        std::cout << "  FAIL: " << name << "\n";
    }
}

int main()
{
    std::cout << "H264MppEncoder verification\n";
    std::cout << "===========================\n\n";

    H264MppEncoder encoder;
    const bool available = encoder.IsAvailable();
    Report("Availability: encoder availability is deterministic",
           available == encoder.IsAvailable());

    H264EncoderConfig invalid_config;
    Report("InvalidConfig: zero size is rejected",
           encoder.Open(invalid_config) == H264EncodeResult::kInvalidInput);

    H264EncoderConfig config;
    config.width = 64;
    config.height = 64;
    config.hor_stride = 64;
    config.ver_stride = 64;
    config.fps = 25;
    config.bitrate = 1000000;
    config.gop = 25;

    const H264EncodeResult open_result = encoder.Open(config);
    if (!available)
    {
        Report("StubUnavailable: open returns encoder_not_available",
               open_result == H264EncodeResult::kEncoderNotAvailable);
    }
    else
    {
        Report("AvailableEncoder: open succeeds", open_result == H264EncodeResult::kOk);
        if (open_result == H264EncodeResult::kOk)
        {
            DecodedImageFrame frame;
            frame.width = config.width;
            frame.height = config.height;
            frame.hor_stride = config.hor_stride;
            frame.ver_stride = config.ver_stride;
            frame.pixel_format = "NV12";
            frame.payload.resize(static_cast<size_t>(config.hor_stride) *
                                 static_cast<size_t>(config.ver_stride) * 3U / 2U,
                                 0x80);
            std::fill(frame.payload.begin(),
                      frame.payload.begin() + static_cast<size_t>(config.hor_stride) *
                                                static_cast<size_t>(config.ver_stride),
                      0x10);

            std::vector<EncodedPacket> packets;
            const H264EncodeResult encode_result = encoder.EncodeFrame(frame, &packets);
            Report("AvailableEncoder: synthetic NV12 frame encodes",
                   encode_result == H264EncodeResult::kOk && !packets.empty());
        }
    }

    Report("ErrorString: unavailable maps to control error",
           std::string(ToErrorString(H264EncodeResult::kEncoderNotAvailable)) ==
               "encoder_not_available");

    encoder.Close();

    std::cout << "\n===========================\n";
    std::cout << "Total: " << (g_pass + g_fail)
              << "  Pass: " << g_pass
              << "  Fail: " << g_fail << "\n";
    return g_fail > 0 ? 1 : 0;
}
