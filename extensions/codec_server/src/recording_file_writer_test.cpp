#include "codec_server/recording_file_writer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using camera_subsystem::extensions::codec_server::RecordingFileWriter;
using camera_subsystem::extensions::codec_server::WriterResult;
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
    std::string base = "/tmp/rfw_test_" + std::to_string(getpid());
    fs::create_directories(base);
    return base;
}

static void RemoveTempDir(const std::string& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Read entire file into a string.
static std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs),
            std::istreambuf_iterator<char>()};
}

// ===========================================================================
// Test cases
// ===========================================================================

static void TestOpenWriteClose()
{
    std::string tmp = MakeTempDir() + "/basic";
    RecordingFileWriter w;
    WriterResult r = w.Open("cam0", tmp);
    Report("OpenWriteClose: Open returns kOk", r == WriterResult::kOk);

    uint8_t data[] = {0x00, 0x00, 0x00, 0x01, 0x67}; // fake SPS NAL
    r = w.Write(data, sizeof(data));
    Report("OpenWriteClose: Write returns kOk", r == WriterResult::kOk);

    WriterStats st = w.GetStats();
    Report("OpenWriteClose: bytes_written == 5", st.bytes_written == 5);
    Report("OpenWriteClose: packets_written == 1", st.packets_written == 1);

    std::string path = w.GetFilePath();
    r = w.Close();
    Report("OpenWriteClose: Close returns kOk", r == WriterResult::kOk);

    // Verify file exists and content matches.
    auto content = ReadFile(path);
    Report("OpenWriteClose: file size == 5", content.size() == 5);
    Report("OpenWriteClose: file content matches",
           content.size() == 5 &&
           std::memcmp(content.data(), data, 5) == 0);

    RemoveTempDir(tmp);
}

static void TestDirAutoCreate()
{
    std::string tmp = MakeTempDir() + "/auto_create_dir/sub";
    RecordingFileWriter w;
    WriterResult r = w.Open("cam1", tmp);
    Report("DirAutoCreate: Open returns kOk", r == WriterResult::kOk);
    Report("DirAutoCreate: directory was created", fs::exists(tmp));
    w.Close();
    RemoveTempDir(MakeTempDir() + "/auto_create_dir");
}

static void TestDirNotWritable()
{
    // /proc is never writable for regular file creation.
    RecordingFileWriter w;
    WriterResult r = w.Open("cam2", "/proc");
    Report("DirNotWritable: Open returns kOutputDirNotWritable",
           r == WriterResult::kOutputDirNotWritable);
}

static void TestInvalidStreamId()
{
    RecordingFileWriter w;
    WriterResult r1 = w.Open("a/b", "/tmp");
    Report("InvalidStreamId: slash returns kInvalidStreamId",
           r1 == WriterResult::kInvalidStreamId);

    WriterResult r2 = w.Open("", "/tmp");
    Report("InvalidStreamId: empty returns kInvalidStreamId",
           r2 == WriterResult::kInvalidStreamId);

    WriterResult r3 = w.Open("a\\b", "/tmp");
    Report("InvalidStreamId: backslash returns kInvalidStreamId",
           r3 == WriterResult::kInvalidStreamId);

    std::string long_id(129, 'x');
    WriterResult r4 = w.Open(long_id, "/tmp");
    Report("InvalidStreamId: too long returns kInvalidStreamId",
           r4 == WriterResult::kInvalidStreamId);
}

static void TestFileNameConflict()
{
    std::string tmp = MakeTempDir() + "/conflict";
    RecordingFileWriter w1;
    WriterResult r1 = w1.Open("cam3", tmp);
    Report("FileNameConflict: first Open kOk", r1 == WriterResult::kOk);
    std::string path1 = w1.GetFilePath();

    // Second open with same stream_id in same dir — should get _2 suffix.
    RecordingFileWriter w2;
    WriterResult r2 = w2.Open("cam3", tmp);
    Report("FileNameConflict: second Open kOk", r2 == WriterResult::kOk);
    std::string path2 = w2.GetFilePath();

    Report("FileNameConflict: paths differ", path1 != path2);
    Report("FileNameConflict: second path contains _2",
           path2.find("_2.h264") != std::string::npos);

    w1.Close();
    w2.Close();
    RemoveTempDir(tmp);
}

static void TestRepeatedOpen()
{
    std::string tmp = MakeTempDir() + "/repeated";
    RecordingFileWriter w;
    WriterResult r1 = w.Open("cam4", tmp);
    Report("RepeatedOpen: first Open kOk", r1 == WriterResult::kOk);
    std::string path1 = w.GetFilePath();

    uint8_t data[] = {0xAA};
    w.Write(data, 1);

    // Second Open should implicitly close the first file.
    WriterResult r2 = w.Open("cam5", tmp);
    Report("RepeatedOpen: second Open kOk", r2 == WriterResult::kOk);
    std::string path2 = w.GetFilePath();
    Report("RepeatedOpen: paths differ", path1 != path2);

    // First file should still exist and have content.
    auto content = ReadFile(path1);
    Report("RepeatedOpen: first file has 1 byte", content.size() == 1);

    w.Close();
    RemoveTempDir(tmp);
}

static void TestWriteNotOpen()
{
    RecordingFileWriter w;
    uint8_t data[] = {0x01};
    WriterResult r = w.Write(data, 1);
    Report("WriteNotOpen: returns kFileNotOpen",
           r == WriterResult::kFileNotOpen);
}

static void TestZeroLengthWrite()
{
    std::string tmp = MakeTempDir() + "/zerowrite";
    RecordingFileWriter w;
    w.Open("cam6", tmp);
    uint8_t dummy = 0;
    WriterResult r = w.Write(&dummy, 0);
    Report("ZeroLengthWrite: returns kOk", r == WriterResult::kOk);

    WriterStats st = w.GetStats();
    Report("ZeroLengthWrite: bytes_written == 0", st.bytes_written == 0);
    Report("ZeroLengthWrite: packets_written == 0", st.packets_written == 0);

    w.Close();
    RemoveTempDir(tmp);
}

static void TestFlush()
{
    std::string tmp = MakeTempDir() + "/flush";
    RecordingFileWriter w;
    w.Open("cam7", tmp);

    uint8_t data[] = {0x00, 0x00, 0x00, 0x01, 0x68}; // fake PPS NAL
    w.Write(data, sizeof(data));

    WriterResult r = w.Flush();
    Report("Flush: returns kOk", r == WriterResult::kOk);

    // After flush, another process should be able to read the data.
    std::string path = w.GetFilePath();
    auto content = ReadFile(path);
    Report("Flush: file has 5 bytes after flush", content.size() == 5);

    w.Close();
    RemoveTempDir(tmp);
}

static void TestFlushNotOpen()
{
    RecordingFileWriter w;
    WriterResult r = w.Flush();
    Report("FlushNotOpen: returns kFileNotOpen",
           r == WriterResult::kFileNotOpen);
}

static void TestCloseIdempotent()
{
    std::string tmp = MakeTempDir() + "/closeidem";
    RecordingFileWriter w;
    w.Open("cam8", tmp);
    WriterResult r1 = w.Close();
    WriterResult r2 = w.Close();
    Report("CloseIdempotent: first Close kOk", r1 == WriterResult::kOk);
    Report("CloseIdempotent: second Close kOk", r2 == WriterResult::kOk);
    RemoveTempDir(tmp);
}

static void TestCloseThenWrite()
{
    std::string tmp = MakeTempDir() + "/closethenwrite";
    RecordingFileWriter w;
    w.Open("cam9", tmp);
    w.Close();
    uint8_t data[] = {0x01};
    WriterResult r = w.Write(data, 1);
    Report("CloseThenWrite: returns kFileNotOpen",
           r == WriterResult::kFileNotOpen);
    RemoveTempDir(tmp);
}

static void TestStatsQuery()
{
    std::string tmp = MakeTempDir() + "/stats";
    RecordingFileWriter w;
    w.Open("cam10", tmp);

    uint8_t data[] = {0x01, 0x02, 0x03};
    w.Write(data, 3);
    w.Write(data, 3);

    WriterStats st = w.GetStats();
    Report("StatsQuery: bytes_written == 6", st.bytes_written == 6);
    Report("StatsQuery: packets_written == 2", st.packets_written == 2);
    Report("StatsQuery: write_failures == 0", st.write_failures == 0);

    std::string path = w.GetFilePath();
    Report("StatsQuery: GetFilePath not empty", !path.empty());
    Report("StatsQuery: GetFilePath ends with .h264",
           path.size() >= 5 &&
           path.substr(path.size() - 5) == ".h264");

    w.Close();

    // Stats preserved after close.
    st = w.GetStats();
    Report("StatsQuery: stats preserved after close",
           st.bytes_written == 6 && st.packets_written == 2);

    RemoveTempDir(tmp);
}

static void TestResetStats()
{
    std::string tmp = MakeTempDir() + "/reset";
    RecordingFileWriter w;
    w.Open("cam11", tmp);
    uint8_t data[] = {0x01};
    w.Write(data, 1);

    w.ResetStats();
    WriterStats st = w.GetStats();
    Report("ResetStats: bytes_written == 0", st.bytes_written == 0);
    Report("ResetStats: packets_written == 0", st.packets_written == 0);
    Report("ResetStats: write_failures == 0", st.write_failures == 0);

    w.Close();
    RemoveTempDir(tmp);
}

// ===========================================================================
// Main
// ===========================================================================

int main()
{
    std::cout << "RecordingFileWriter verification\n";
    std::cout << "===============================\n\n";

    TestOpenWriteClose();
    TestDirAutoCreate();
    TestDirNotWritable();
    TestInvalidStreamId();
    TestFileNameConflict();
    TestRepeatedOpen();
    TestWriteNotOpen();
    TestZeroLengthWrite();
    TestFlush();
    TestFlushNotOpen();
    TestCloseIdempotent();
    TestCloseThenWrite();
    TestStatsQuery();
    TestResetStats();

    std::cout << "\n===============================\n";
    std::cout << "Total: " << (g_pass + g_fail)
              << "  Pass: " << g_pass
              << "  Fail: " << g_fail << "\n";

    return g_fail > 0 ? 1 : 0;
}
