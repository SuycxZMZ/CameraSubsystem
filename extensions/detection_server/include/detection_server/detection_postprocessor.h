#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_POSTPROCESSOR_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_POSTPROCESSOR_H

#include "detection_server/detection_types.h"

#include <string>
#include <vector>

namespace camera_subsystem::extensions::detection_server {

class DetectionPostprocessor
{
  public:
    bool LoadLabels(const std::string& labels_path, std::string* error_message);
    bool Process(const RknnRuntimeInfo& runtime_info,
                 const std::vector<DetectionOutputTensor>& outputs,
                 const DetectionLetterboxInfo& letterbox,
                 float score_threshold,
                 float nms_threshold,
                 std::vector<DetectionBox>* boxes,
                 std::string* error_message) const;

  private:
    std::vector<std::string> labels_;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_POSTPROCESSOR_H
