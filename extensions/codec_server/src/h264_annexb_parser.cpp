#include "codec_server/h264_annexb_parser.h"

#include <algorithm>

namespace camera_subsystem::extensions::codec_server {
namespace {

struct StartCode
{
    size_t offset = 0;
    size_t size = 0;
};

bool FindStartCode(const uint8_t* data, size_t size, size_t from, StartCode* out)
{
    if (!data || !out || size < 3 || from >= size)
    {
        return false;
    }
    for (size_t i = from; i + 3 <= size; ++i)
    {
        if (data[i] != 0 || data[i + 1] != 0)
        {
            continue;
        }
        if (data[i + 2] == 1)
        {
            *out = StartCode{i, 3};
            return true;
        }
        if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1)
        {
            *out = StartCode{i, 4};
            return true;
        }
    }
    return false;
}

size_t TrimTrailingZeros(const uint8_t* data, size_t begin, size_t end)
{
    while (end > begin && data[end - 1] == 0)
    {
        --end;
    }
    return end;
}

} // namespace

std::vector<H264NalUnit> ParseAnnexBNalUnits(const uint8_t* data, size_t size)
{
    std::vector<H264NalUnit> units;
    StartCode current;
    if (!FindStartCode(data, size, 0, &current))
    {
        return units;
    }

    while (true)
    {
        const size_t payload_begin = current.offset + current.size;
        StartCode next;
        const bool has_next = FindStartCode(data, size, payload_begin, &next);
        const size_t payload_end =
            TrimTrailingZeros(data, payload_begin, has_next ? next.offset : size);
        if (payload_end > payload_begin)
        {
            H264NalUnit unit;
            unit.data = data + payload_begin;
            unit.size = payload_end - payload_begin;
            unit.nal_type = unit.data[0] & 0x1fU;
            units.push_back(unit);
        }
        if (!has_next)
        {
            break;
        }
        current = next;
    }

    return units;
}

H264ParameterSets ExtractH264ParameterSets(const uint8_t* data, size_t size)
{
    H264ParameterSets sets;
    for (const H264NalUnit& unit : ParseAnnexBNalUnits(data, size))
    {
        if (unit.nal_type == static_cast<uint8_t>(H264NalType::kSps))
        {
            sets.sps.assign(unit.data, unit.data + unit.size);
        }
        else if (unit.nal_type == static_cast<uint8_t>(H264NalType::kPps))
        {
            sets.pps.assign(unit.data, unit.data + unit.size);
        }
    }
    return sets;
}

bool IsH264VideoSlice(uint8_t nal_type)
{
    return nal_type == static_cast<uint8_t>(H264NalType::kSliceNonIdr) ||
           nal_type == static_cast<uint8_t>(H264NalType::kSliceIdr);
}

} // namespace camera_subsystem::extensions::codec_server
