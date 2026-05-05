#include "codec_server/codec_server_app.h"

#include "codec_server/codec_control_server.h"

#include <atomic>
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace camera_subsystem::extensions::codec_server {
namespace {

std::atomic<bool> g_running(true);

void SignalHandler(int)
{
    g_running.store(false);
}

} // namespace

CodecServerApp::CodecServerApp(CodecServerConfig config)
    : config_(std::move(config)),
      session_manager_(RecordingSessionConfig{
          config_.output_dir,
          CameraStreamSubscriberConfig{
              config_.control_socket,
              config_.data_socket,
              config_.device_path,
              "camera_codec_server",
              0,
              64U * 1024U * 1024U,
              nullptr},
          true,
          config_.fps,
          config_.bitrate,
          config_.gop})
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

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    CodecControlServer control_server(&session_manager_);
    if (!control_server.Start(config_.codec_socket))
    {
        std::cerr << "failed to start codec control server: "
                  << config_.codec_socket << "\n";
        return 1;
    }

    std::cout << "camera_codec_server control server ready: "
              << config_.codec_socket << "\n";
    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    control_server.Stop();
    std::cout << "camera_codec_server stopped\n";
    return 0;
}

} // namespace camera_subsystem::extensions::codec_server
