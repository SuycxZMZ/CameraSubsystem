#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_DETECTION_SERVER_APP_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_DETECTION_SERVER_APP_H

#include "detection_server/camera_frame_subscriber.h"
#include "detection_server/detection_publisher.h"
#include "detection_server/detection_config.h"
#include "detection_server/detection_postprocessor.h"
#include "detection_server/detection_session.h"
#include "detection_server/frame_preprocessor.h"

#include <atomic>
#include <mutex>
#include <memory>

namespace camera_subsystem::extensions::detection_server {

class CameraDetectionServerApp
{
  public:
    explicit CameraDetectionServerApp(DetectionServerConfig config);

    int Run();

  private:
    bool StartComponents();
    void StopComponents();
    void OnFrame(const camera_subsystem::ipc::CameraDataFrameHeader& header,
                 const std::vector<uint8_t>& payload);
    void LogPeriodicSummary() const;

    DetectionServerConfig config_;
    DetectionSession session_;
    CameraFrameSubscriber subscriber_;
    DetectionPublisher publisher_;
    DetectionPostprocessor postprocessor_;
    std::unique_ptr<IFramePreprocessor> frame_preprocessor_;

    std::atomic<uint64_t> callback_frames_{0};
    std::atomic<uint64_t> callback_bytes_{0};
    std::atomic<uint64_t> publish_failures_{0};
    std::atomic<uint64_t> inferred_frames_{0};
    std::atomic<uint64_t> preprocess_failures_{0};
    std::atomic<uint64_t> inference_failures_{0};
    std::atomic<uint64_t> postprocess_failures_{0};
    std::atomic<uint32_t> last_object_count_{0};

    mutable std::mutex last_frame_mutex_;
    uint64_t last_frame_id_ = 0;
    uint64_t last_timestamp_ns_ = 0;
    uint32_t last_width_ = 0;
    uint32_t last_height_ = 0;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_DETECTION_SERVER_APP_H
