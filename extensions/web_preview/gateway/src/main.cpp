#include "web_preview/camera_subscriber_client.h"
#include "web_preview/frame_pipeline.h"
#include "web_preview/gateway_config.h"
#include "web_preview/web_server.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

namespace {

std::atomic<bool> g_running(true);

void HandleSignal(int)
{
    g_running.store(false);
}

} // namespace

int main(int argc, char* argv[])
{
    signal(SIGINT, HandleSignal);
    signal(SIGTERM, HandleSignal);

    web_preview::GatewayConfig config;
    if (!web_preview::ParseGatewayConfig(argc, argv, &config))
    {
        return 1;
    }

    web_preview::WebServer web_server;
    if (!web_server.Start(config))
    {
        return 1;
    }

    web_preview::FramePipeline pipeline;
    pipeline.SetMaxFps(config.max_preview_fps);
    pipeline.SetPacketCallback([&web_server](const std::vector<uint8_t>& packet) {
        web_server.BroadcastBinary(packet);
    });
    pipeline.SetStatusCallback([&web_server](const web_preview::StreamStats& stats) {
        web_server.UpdateStats(stats);
    });

    web_preview::CameraSubscriberClient camera_client;
    if (!camera_client.Start(
            config,
            [&pipeline](web_preview::CameraFrame&& frame) {
                pipeline.SubmitFrame(std::move(frame));
            },
            [](const std::string& status) {
                std::cerr << "camera status: " << status << "\n";
            }))
    {
        web_server.Stop();
        return 1;
    }

    std::cout << "web_preview_gateway listening on " << config.bind_host << ":"
              << config.http_port << "\n";
    std::cout << "device=" << config.device_path
              << " static_root=" << config.static_root << "\n";

    while (g_running.load() && camera_client.IsRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    camera_client.Stop();
    web_server.Stop();
    return 0;
}
