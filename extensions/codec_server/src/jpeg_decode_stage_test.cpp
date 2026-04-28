#include "codec_server/jpeg_decode_stage.h"

#include <cstdint>
#include <iostream>
#include <string>

using camera_subsystem::extensions::codec_server::DecodedImageFrame;
using camera_subsystem::extensions::codec_server::JpegDecodeResult;
using camera_subsystem::extensions::codec_server::JpegDecodeStage;
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
    std::cout << "JpegDecodeStage verification\n";
    std::cout << "============================\n\n";

    JpegDecodeStage decoder;
    DecodedImageFrame frame;
    const uint8_t data[] = {0xff, 0xd8, 0xff, 0xd9};

    Report("StubUnavailable: decoder reports unavailable", !decoder.IsAvailable());
    Report("StubUnavailable: valid-looking input returns decoder_not_available",
           decoder.Decode(data, sizeof(data), &frame) ==
               JpegDecodeResult::kDecoderNotAvailable);
    Report("InvalidInput: null data returns invalid input",
           decoder.Decode(nullptr, sizeof(data), &frame) ==
               JpegDecodeResult::kInvalidInput);
    Report("InvalidInput: empty input returns invalid input",
           decoder.Decode(data, 0, &frame) == JpegDecodeResult::kInvalidInput);
    Report("ErrorString: unavailable maps to control error",
           std::string(ToErrorString(JpegDecodeResult::kDecoderNotAvailable)) ==
               "jpeg_decoder_not_available");

    std::cout << "\n============================\n";
    std::cout << "Total: " << (g_pass + g_fail)
              << "  Pass: " << g_pass
              << "  Fail: " << g_fail << "\n";
    return g_fail > 0 ? 1 : 0;
}
