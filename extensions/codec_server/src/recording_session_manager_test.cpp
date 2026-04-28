#include "codec_server/recording_session_manager.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using camera_subsystem::extensions::codec_server::CodecControlCommand;
using camera_subsystem::extensions::codec_server::CodecControlRequest;
using camera_subsystem::extensions::codec_server::RecordingSessionConfig;
using camera_subsystem::extensions::codec_server::RecordingSessionManager;

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
    const std::string dir = "/tmp/rsm_test_" + std::to_string(getpid());
    fs::create_directories(dir);
    return dir;
}

static CodecControlRequest MakeRequest(CodecControlCommand command,
                                       const std::string& stream_id,
                                       const std::string& output_dir)
{
    CodecControlRequest request;
    request.command = command;
    request.request_id = "test-1";
    request.stream_id = stream_id;
    request.output_dir = output_dir;
    return request;
}

static void TestStartStopStatus()
{
    const std::string dir = MakeTempDir() + "/recordings";
    RecordingSessionConfig config;
    config.default_output_dir = dir;
    RecordingSessionManager manager(config);

    auto start = MakeRequest(CodecControlCommand::kStartRecording, "cam0", "");
    auto status = manager.StartRecording(start);
    Report("StartStopStatus: start enters recording",
           status.recording && status.state == "recording");
    Report("StartStopStatus: file path is h264",
           status.file.size() >= 5 &&
               status.file.substr(status.file.size() - 5) == ".h264");

    auto duplicate = manager.StartRecording(start);
    Report("StartStopStatus: duplicate start returns already_recording",
           duplicate.error == "already_recording");

    auto query = MakeRequest(CodecControlCommand::kStatus, "cam0", "");
    status = manager.GetStatus(query);
    Report("StartStopStatus: status remains recording",
           status.recording && status.state == "recording");

    auto stop = MakeRequest(CodecControlCommand::kStopRecording, "cam0", "");
    status = manager.StopRecording(stop);
    Report("StartStopStatus: stop enters idle",
           !status.recording && status.state == "idle" && status.error.empty());

    auto stop_again = manager.StopRecording(stop);
    Report("StartStopStatus: second stop returns not_recording",
           stop_again.error == "not_recording");

    fs::remove_all(dir);
}

static void TestInvalidStreamId()
{
    const std::string dir = MakeTempDir() + "/invalid";
    RecordingSessionConfig config;
    config.default_output_dir = dir;
    RecordingSessionManager manager(config);
    auto request = MakeRequest(CodecControlCommand::kStartRecording, "a/b", "");
    auto status = manager.StartRecording(request);
    Report("InvalidStreamId: returns invalid_stream_id",
           status.state == "error" && status.error == "invalid_stream_id");
    fs::remove_all(dir);
}

int main()
{
    std::cout << "RecordingSessionManager verification\n";
    std::cout << "====================================\n\n";

    TestStartStopStatus();
    TestInvalidStreamId();

    std::cout << "\n====================================\n";
    std::cout << "Total: " << (g_pass + g_fail)
              << "  Pass: " << g_pass
              << "  Fail: " << g_fail << "\n";
    return g_fail > 0 ? 1 : 0;
}
