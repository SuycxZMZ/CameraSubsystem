#ifndef WEB_PREVIEW_GATEWAY_CONFIG_H
#define WEB_PREVIEW_GATEWAY_CONFIG_H

#include <cstdint>
#include <string>

namespace web_preview {

struct GatewayConfig
{
    std::string bind_host = "0.0.0.0";
    uint16_t http_port = 8080;
    std::string control_socket = "/tmp/camera_subsystem_control.sock";
    std::string data_socket = "/tmp/camera_subsystem_data.sock";
    std::string codec_socket = "/tmp/camera_subsystem_codec.sock";
    std::string device_path = "/dev/video0";
    std::string static_root = "../web/dist";
    std::string client_id = "web_preview_gateway";
    std::string output_dir = "/home/luckfox/CameraSubsystem/recordings";
    uint32_t camera_id = 0;
    uint32_t max_preview_fps = 15;
};

bool ParseGatewayConfig(int argc, char* argv[], GatewayConfig* config);
void PrintUsage(const char* program_name);

} // namespace web_preview

#endif // WEB_PREVIEW_GATEWAY_CONFIG_H
