#include "codec_server/codec_server_app.h"

#include <iostream>
#include <utility>

namespace camera_subsystem::extensions::codec_server {

CodecServerApp::CodecServerApp(CodecServerConfig config)
    : config_(std::move(config))
{
}

int CodecServerApp::Run()
{
    std::cout << "camera_codec_server starting\n"
              << "  control_socket=" << config_.control_socket << "\n"
              << "  data_socket=" << config_.data_socket << "\n"
              << "  release_socket=" << config_.release_socket << "\n"
              << "  codec_socket=" << config_.codec_socket << "\n"
              << "  device=" << config_.device_path << "\n"
              << "  camera_id=" << config_.camera_id << "\n"
              << "  output_dir=" << config_.output_dir << "\n"
              << "  input_format=" << config_.input_format << "\n"
              << "  data_plane=" << config_.data_plane << "\n"
              << "  codec=" << config_.codec << "\n"
              << "  width=" << config_.width << "\n"
              << "  height=" << config_.height << "\n"
              << "  fps=" << config_.fps << "\n"
              << "  bitrate=" << config_.bitrate << "\n"
              << "  gop=" << config_.gop << "\n";

    std::cout << "camera_codec_server scaffold ready; control server is not implemented yet\n";
    return 0;
}

} // namespace camera_subsystem::extensions::codec_server
