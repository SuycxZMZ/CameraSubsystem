#include "codec_server/mp4_file_writer.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using camera_subsystem::extensions::codec_server::Mp4FileWriter;
using camera_subsystem::extensions::codec_server::Mp4WriterConfig;
using camera_subsystem::extensions::codec_server::Mp4WriterResult;
using camera_subsystem::extensions::codec_server::WriterStats;

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

static std::string MakeTempDir()
{
    std::string base = "/tmp/mp4_writer_test_" + std::to_string(getpid());
    fs::create_directories(base);
    return base;
}

static void RemoveTempDir(const std::string& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs),
            std::istreambuf_iterator<char>()};
}

static bool ContainsString(const std::vector<uint8_t>& data, const char* value)
{
    const std::string needle(value);
    if (needle.empty() || data.size() < needle.size())
    {
        return false;
    }
    return std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end();
}

static bool RunFfprobe(const std::string& path)
{
    const std::string command =
        "ffprobe -v error -select_streams v:0 -show_entries "
        "stream=codec_name,width,height,nb_frames -of default=nw=1 '" + path +
        "' >/tmp/mp4_writer_ffprobe.out 2>/tmp/mp4_writer_ffprobe.err";
    const int rc = std::system(command.c_str());
    return rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

static void TestWriteProbeableMp4()
{
    const std::string tmp = MakeTempDir() + "/basic";
    Mp4FileWriter writer;
    Mp4WriterConfig config;
    config.width = 16;
    config.height = 16;
    config.fps = 25;

    Mp4WriterResult r = writer.Open("cam0", tmp, config);
    Report("WriteProbeableMp4: Open returns kOk", r == Mp4WriterResult::kOk);

    const uint8_t sps_pps[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a, 0xf8, 0x41,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x06, 0xe2,
    };
    r = writer.WriteAnnexBPacket(sps_pps, sizeof(sps_pps));
    Report("WriteProbeableMp4: parameter packet accepted", r == Mp4WriterResult::kOk);

    const uint8_t idr[] = {
        0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x21, 0xa0,
    };
    r = writer.WriteAnnexBPacket(idr, sizeof(idr));
    Report("WriteProbeableMp4: IDR packet accepted", r == Mp4WriterResult::kOk);

    const uint8_t pframe[] = {
        0x00, 0x00, 0x01, 0x41, 0x9a, 0x22, 0x11,
    };
    r = writer.WriteAnnexBPacket(pframe, sizeof(pframe));
    Report("WriteProbeableMp4: P packet accepted", r == Mp4WriterResult::kOk);

    const std::string path = writer.GetFilePath();
    WriterStats stats = writer.GetStats();
    Report("WriteProbeableMp4: media packets == 2", stats.packets_written == 2);

    r = writer.Close();
    Report("WriteProbeableMp4: Close returns kOk", r == Mp4WriterResult::kOk);
    Report("WriteProbeableMp4: file ends with .mp4",
           path.size() >= 4 && path.substr(path.size() - 4) == ".mp4");

    const std::vector<uint8_t> content = ReadFile(path);
    Report("WriteProbeableMp4: file not empty", !content.empty());
    Report("WriteProbeableMp4: contains ftyp", ContainsString(content, "ftyp"));
    Report("WriteProbeableMp4: contains mdat", ContainsString(content, "mdat"));
    Report("WriteProbeableMp4: contains moov", ContainsString(content, "moov"));
    Report("WriteProbeableMp4: ffprobe accepts file", RunFfprobe(path));

    RemoveTempDir(tmp);
}

static void TestMissingParameterSets()
{
    const std::string tmp = MakeTempDir() + "/missing_ps";
    Mp4FileWriter writer;
    Mp4WriterConfig config;
    config.width = 16;
    config.height = 16;

    Mp4WriterResult r = writer.Open("cam1", tmp, config);
    Report("MissingParameterSets: Open returns kOk", r == Mp4WriterResult::kOk);

    const uint8_t idr[] = {0x00, 0x00, 0x01, 0x65, 0x88};
    r = writer.WriteAnnexBPacket(idr, sizeof(idr));
    Report("MissingParameterSets: packet accepted", r == Mp4WriterResult::kOk);
    r = writer.Close();
    Report("MissingParameterSets: Close returns kMissingParameterSets",
           r == Mp4WriterResult::kMissingParameterSets);
    Report("MissingParameterSets: write failure recorded",
           writer.GetStats().write_failures == 1);

    RemoveTempDir(tmp);
}

static void TestInvalidOpen()
{
    Mp4FileWriter writer;
    Mp4WriterConfig config;
    config.width = 0;
    config.height = 16;
    Mp4WriterResult r = writer.Open("cam2", "/tmp", config);
    Report("InvalidOpen: zero width returns kInvalidPacket",
           r == Mp4WriterResult::kInvalidPacket);
}

int main()
{
    std::cout << "Mp4FileWriter verification\n";
    std::cout << "==========================\n\n";

    TestWriteProbeableMp4();
    TestMissingParameterSets();
    TestInvalidOpen();

    std::cout << "\n==========================\n";
    std::cout << "Total: " << (g_pass + g_fail)
              << "  Pass: " << g_pass
              << "  Fail: " << g_fail << "\n";

    return g_fail > 0 ? 1 : 0;
}
