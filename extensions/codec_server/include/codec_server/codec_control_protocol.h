#ifndef CODEC_SERVER_CODEC_CONTROL_PROTOCOL_H
#define CODEC_SERVER_CODEC_CONTROL_PROTOCOL_H

#include <cstdint>
#include <string>

namespace camera_subsystem::extensions::codec_server {

enum class CodecControlCommand
{
    kStartRecording,
    kStopRecording,
    kStatus,
    kUnknown,
};

struct CodecControlProfile
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    uint32_t bitrate = 0;
    uint32_t gop = 0;
};

struct CodecControlRequest
{
    CodecControlCommand command = CodecControlCommand::kUnknown;
    std::string request_id;
    std::string stream_id;
    std::string codec = "h264";
    std::string container = "raw_h264";
    std::string output_dir;
    CodecControlProfile profile;
};

struct CodecControlStatus
{
    std::string request_id;
    std::string stream_id;
    bool recording = false;
    std::string state = "idle";
    std::string codec = "h264";
    std::string container = "raw_h264";
    std::string file;
    uint64_t encoded_frames = 0;
    uint64_t decoded_frames = 0;
    uint64_t dropped_frames = 0;
    uint64_t input_frames = 0;
    uint64_t decode_failures = 0;
    uint64_t write_failures = 0;
    std::string error;
    CodecControlProfile profile;
};

bool ParseCodecControlRequestLine(const std::string& line,
                                  CodecControlRequest* request,
                                  std::string* error);
std::string SerializeCodecControlStatus(const CodecControlStatus& status);

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_CODEC_CONTROL_PROTOCOL_H
