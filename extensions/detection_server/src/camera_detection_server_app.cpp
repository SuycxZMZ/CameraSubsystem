#include "detection_server/camera_detection_server_app.h"

#include "camera_subsystem/platform/platform_logger.h"
#include "detection_server/detection_types.h"

#include <utility>

namespace camera_subsystem::extensions::detection_server {

CameraDetectionServerApp::CameraDetectionServerApp(DetectionServerConfig config)
    : config_(std::move(config))
{
}

int CameraDetectionServerApp::Run()
{
    using camera_subsystem::core::LogLevel;
    using camera_subsystem::platform::PlatformLogger;

    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "detection_server",
                        "camera_detection_server skeleton ready: stream=%s model=%s "
                        "core_mask=%u profile=%s",
                        config_.stream_id.c_str(), config_.model_path.c_str(),
                        config_.npu_core_mask,
                        PerformanceProfileToString(config_.performance_profile));
    PlatformLogger::Log(LogLevel::kInfo, "detection_server",
                        "current stage only wires config parsing and performance profile "
                        "manager; RKNN/session wiring pending");

    PlatformLogger::Shutdown();
    return 0;
}

} // namespace camera_subsystem::extensions::detection_server
