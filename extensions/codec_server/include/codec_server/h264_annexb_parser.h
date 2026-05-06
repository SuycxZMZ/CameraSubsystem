#ifndef CODEC_SERVER_H264_ANNEXB_PARSER_H
#define CODEC_SERVER_H264_ANNEXB_PARSER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace camera_subsystem::extensions::codec_server {

enum class H264NalType : uint8_t
{
    kUnspecified = 0,
    kSliceNonIdr = 1,
    kSliceIdr = 5,
    kSei = 6,
    kSps = 7,
    kPps = 8,
    kAud = 9,
};

struct H264NalUnit
{
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint8_t nal_type = 0;
};

struct H264ParameterSets
{
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
};

std::vector<H264NalUnit> ParseAnnexBNalUnits(const uint8_t* data, size_t size);
H264ParameterSets ExtractH264ParameterSets(const uint8_t* data, size_t size);
bool IsH264VideoSlice(uint8_t nal_type);

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_H264_ANNEXB_PARSER_H
