#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_CONFIG_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_CONFIG_H

#include "detection_server/detection_types.h"

#include <cstdint>
#include <string>

namespace camera_subsystem::extensions::detection_server {

struct DetectionServerConfig
{
    std::string control_socket = "/tmp/camera_subsystem_detection.sock";
    std::string result_socket = "/tmp/camera_subsystem_detection_result.sock";
    std::string stream_id = "usb0";
    std::string model_path = "/home/luckfox/CameraSubsystem/models/yolo11n.rknn";
    std::string labels_path = "/home/luckfox/CameraSubsystem/models/coco_80_labels.txt";
    uint32_t npu_core_mask = 1;
    double score_threshold = 0.25;
    double nms_threshold = 0.45;
    DetectionOutputMode output_mode = DetectionOutputMode::kMetadataAndAnnotatedFrame;
    uint32_t infer_every_n_frames = 1;
    bool draw_boxes = true;
    uint32_t max_queue_depth = 2;
    PerformanceProfile performance_profile = PerformanceProfile::kNpuCpu;
    bool allow_performance_profile_failure = false;
    uint32_t metrics_interval_ms = 1000;

    bool IsValid(std::string* reason) const;
};

enum class ParseResult
{
    kOk,
    kHelp,
    kError,
};

ParseResult ParseDetectionServerConfig(int argc, char* argv[], DetectionServerConfig* config);
void PrintDetectionServerUsage(const char* program_name);

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_CONFIG_H
