#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_FRAME_PREPROCESSOR_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_FRAME_PREPROCESSOR_H

#include "camera_subsystem/ipc/camera_data_ipc.h"
#include "detection_server/detection_types.h"

#include <string>
#include <vector>

namespace camera_subsystem::extensions::detection_server {

struct PreprocessedFrame
{
    DetectionInputTensor input_tensor;
    DetectionLetterboxInfo letterbox;
};

class IFramePreprocessor
{
  public:
    virtual ~IFramePreprocessor() = default;

    virtual bool SupportsFrame(uint32_t pixel_format) const = 0;
    virtual bool Preprocess(const camera_subsystem::ipc::CameraDataFrameHeader& header,
                            const std::vector<uint8_t>& payload,
                            const RknnRuntimeInfo& runtime_info,
                            PreprocessedFrame* output,
                            std::string* error_message) const = 0;
};

class MjpegFramePreprocessor final : public IFramePreprocessor
{
  public:
    bool SupportsFrame(uint32_t pixel_format) const override;
    bool Preprocess(const camera_subsystem::ipc::CameraDataFrameHeader& header,
                    const std::vector<uint8_t>& payload,
                    const RknnRuntimeInfo& runtime_info,
                    PreprocessedFrame* output,
                    std::string* error_message) const override;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_FRAME_PREPROCESSOR_H
