#include "codec_server/h264_annexb_parser.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using camera_subsystem::extensions::codec_server::ExtractH264ParameterSets;
using camera_subsystem::extensions::codec_server::H264NalType;
using camera_subsystem::extensions::codec_server::IsH264VideoSlice;
using camera_subsystem::extensions::codec_server::ParseAnnexBNalUnits;

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

static bool BytesEqual(const uint8_t* lhs,
                       size_t lhs_size,
                       const std::vector<uint8_t>& rhs)
{
    return lhs_size == rhs.size() &&
           (lhs_size == 0 || std::memcmp(lhs, rhs.data(), lhs_size) == 0);
}

static void TestMixedStartCodes()
{
    const uint8_t data[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00,
        0x00, 0x00, 0x01, 0x68, 0xee,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x99,
    };
    const auto units = ParseAnnexBNalUnits(data, sizeof(data));
    Report("MixedStartCodes: unit count == 3", units.size() == 3);
    Report("MixedStartCodes: first is SPS",
           units.size() == 3 &&
           units[0].nal_type == static_cast<uint8_t>(H264NalType::kSps));
    Report("MixedStartCodes: second is PPS",
           units.size() == 3 &&
           units[1].nal_type == static_cast<uint8_t>(H264NalType::kPps));
    Report("MixedStartCodes: third is IDR",
           units.size() == 3 &&
           units[2].nal_type == static_cast<uint8_t>(H264NalType::kSliceIdr));
}

static void TestLeadingBytesAndTrailingZeros()
{
    const uint8_t data[] = {
        0xaa, 0xbb,
        0x00, 0x00, 0x01, 0x06, 0x05, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x41, 0x9a, 0x00, 0x00,
    };
    const auto units = ParseAnnexBNalUnits(data, sizeof(data));
    Report("LeadingBytesAndTrailingZeros: unit count == 2", units.size() == 2);
    Report("LeadingBytesAndTrailingZeros: trims first trailing zeros",
           units.size() == 2 && units[0].size == 2);
    Report("LeadingBytesAndTrailingZeros: trims final trailing zeros",
           units.size() == 2 && units[1].size == 2);
}

static void TestExtractParameterSets()
{
    const uint8_t data[] = {
        0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x28,
        0x00, 0x00, 0x01, 0x68, 0xee, 0x3c,
        0x00, 0x00, 0x01, 0x65, 0x88,
    };
    const auto sets = ExtractH264ParameterSets(data, sizeof(data));
    Report("ExtractParameterSets: SPS matches",
           BytesEqual(sets.sps.data(), sets.sps.size(), {0x67, 0x64, 0x00, 0x28}));
    Report("ExtractParameterSets: PPS matches",
           BytesEqual(sets.pps.data(), sets.pps.size(), {0x68, 0xee, 0x3c}));
}

static void TestNoStartCode()
{
    const uint8_t data[] = {0x67, 0x64, 0x00, 0x28};
    const auto units = ParseAnnexBNalUnits(data, sizeof(data));
    Report("NoStartCode: returns empty", units.empty());
}

static void TestSliceClassifier()
{
    Report("SliceClassifier: non-IDR is slice",
           IsH264VideoSlice(static_cast<uint8_t>(H264NalType::kSliceNonIdr)));
    Report("SliceClassifier: IDR is slice",
           IsH264VideoSlice(static_cast<uint8_t>(H264NalType::kSliceIdr)));
    Report("SliceClassifier: SPS is not slice",
           !IsH264VideoSlice(static_cast<uint8_t>(H264NalType::kSps)));
}

int main()
{
    std::cout << "H264AnnexBParser verification\n";
    std::cout << "=============================\n\n";

    TestMixedStartCodes();
    TestLeadingBytesAndTrailingZeros();
    TestExtractParameterSets();
    TestNoStartCode();
    TestSliceClassifier();

    std::cout << "\n=============================\n";
    std::cout << "Total: " << (g_pass + g_fail)
              << "  Pass: " << g_pass
              << "  Fail: " << g_fail << "\n";

    return g_fail > 0 ? 1 : 0;
}
