#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_SESSION_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_SESSION_H

#include "detection_server/detection_config.h"
#include "detection_server/performance_profile_manager.h"
#include "detection_server/rknn_model_session.h"

#include <memory>
#include <mutex>

namespace camera_subsystem::extensions::detection_server {

class DetectionSession
{
  public:
    DetectionSession(DetectionServerConfig config, std::unique_ptr<IRknnModelSession> model_session,
                     std::shared_ptr<const IPerformanceProfileManager> profile_manager);
    DetectionSession(DetectionServerConfig config, std::string sysfs_root = std::string());

    bool Start();
    bool Stop();

    DetectionState GetState() const;
    DetectionSessionSnapshot GetSnapshot() const;

  private:
    void SetErrorLocked(DetectionErrorCode error_code, std::string error_message);

    DetectionServerConfig config_;
    std::unique_ptr<IRknnModelSession> model_session_;
    std::shared_ptr<const IPerformanceProfileManager> profile_manager_;

    mutable std::mutex mutex_;
    DetectionSessionSnapshot snapshot_;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_SESSION_H
