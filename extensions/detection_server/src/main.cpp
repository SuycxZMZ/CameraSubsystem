#include "detection_server/camera_detection_server_app.h"
#include "detection_server/detection_config.h"

#include <utility>

int main(int argc, char* argv[])
{
    camera_subsystem::extensions::detection_server::DetectionServerConfig config;
    const auto parse_result =
        camera_subsystem::extensions::detection_server::ParseDetectionServerConfig(argc, argv,
                                                                                   &config);

    if (parse_result == camera_subsystem::extensions::detection_server::ParseResult::kHelp)
    {
        return 0;
    }
    if (parse_result == camera_subsystem::extensions::detection_server::ParseResult::kError)
    {
        return 1;
    }

    camera_subsystem::extensions::detection_server::CameraDetectionServerApp app(std::move(config));
    return app.Run();
}
