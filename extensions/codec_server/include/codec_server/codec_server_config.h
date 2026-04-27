#ifndef CODEC_SERVER_CODEC_SERVER_CONFIG_H
#define CODEC_SERVER_CODEC_SERVER_CONFIG_H

#include <cstdint>
#include <string>

namespace camera_subsystem::extensions::codec_server {

struct CodecServerConfig
{
    std::string control_socket = "/tmp/camera_subsystem_control.sock";
    std::string data_socket = "/tmp/camera_subsystem_data.sock";
    std::string release_socket = "/tmp/camera_subsystem_release_v2.sock";
    std::string codec_socket = "/tmp/camera_subsystem_codec.sock";
    std::string device_path = "/dev/video45";
    std::string camera_id = "default_camera";
    std::string output_dir = "/home/luckfox/recordings";
    std::string input_format = "auto";
    std::string codec = "h264";
    std::string data_plane = "v1";
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate = 4000000;
    uint32_t gop = 60;
};

enum class ParseResult
{
    kOk,
    kHelp,
    kError,
};

ParseResult ParseCodecServerConfig(int argc, char* argv[], CodecServerConfig* config);
void PrintCodecServerUsage(const char* program_name);

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_CODEC_SERVER_CONFIG_H
