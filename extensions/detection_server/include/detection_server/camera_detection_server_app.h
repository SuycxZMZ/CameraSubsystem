#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_DETECTION_SERVER_APP_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_DETECTION_SERVER_APP_H

#include "detection_server/detection_config.h"

namespace camera_subsystem::extensions::detection_server {

class CameraDetectionServerApp
{
  public:
    explicit CameraDetectionServerApp(DetectionServerConfig config);

    int Run();

  private:
    DetectionServerConfig config_;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_DETECTION_SERVER_APP_H
