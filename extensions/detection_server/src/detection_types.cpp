#include "detection_server/detection_types.h"

#include <array>

namespace camera_subsystem::extensions::detection_server {

const char* DetectionStateToString(DetectionState state)
{
    switch (state)
    {
    case DetectionState::kIdle:
        return "idle";
    case DetectionState::kStarting:
        return "starting";
    case DetectionState::kRunning:
        return "running";
    case DetectionState::kDegraded:
        return "degraded";
    case DetectionState::kError:
        return "error";
    case DetectionState::kStopping:
        return "stopping";
    }

    return "unknown";
}

const char* DetectionErrorCodeToString(DetectionErrorCode error_code)
{
    switch (error_code)
    {
    case DetectionErrorCode::kOk:
        return "OK";
    case DetectionErrorCode::kInvalidConfig:
        return "INVALID_CONFIG";
    case DetectionErrorCode::kPerformanceProfileFailed:
        return "PERFORMANCE_PROFILE_FAILED";
    case DetectionErrorCode::kModelLoadFailed:
        return "MODEL_LOAD_FAILED";
    case DetectionErrorCode::kCoreMaskFailed:
        return "CORE_MASK_FAILED";
    case DetectionErrorCode::kSubscribeFailed:
        return "SUBSCRIBE_FAILED";
    case DetectionErrorCode::kFrameDecodeFailed:
        return "FRAME_DECODE_FAILED";
    case DetectionErrorCode::kInferenceFailed:
        return "INFERENCE_FAILED";
    case DetectionErrorCode::kPublishFailed:
        return "PUBLISH_FAILED";
    }

    return "UNKNOWN_ERROR";
}

const char* PerformanceProfileToString(PerformanceProfile profile)
{
    switch (profile)
    {
    case PerformanceProfile::kNone:
        return "none";
    case PerformanceProfile::kNpu:
        return "npu";
    case PerformanceProfile::kNpuCpu:
        return "npu-cpu";
    case PerformanceProfile::kFull:
        return "full";
    }

    return "unknown";
}

const char* DetectionOutputModeToString(DetectionOutputMode mode)
{
    switch (mode)
    {
    case DetectionOutputMode::kMetadataOnly:
        return "metadata_only";
    case DetectionOutputMode::kMetadataAndAnnotatedFrame:
        return "metadata_and_annotated_frame";
    }

    return "unknown";
}

bool ParsePerformanceProfile(const std::string& value, PerformanceProfile* profile)
{
    if (!profile)
    {
        return false;
    }

    if (value == "none")
    {
        *profile = PerformanceProfile::kNone;
        return true;
    }
    if (value == "npu")
    {
        *profile = PerformanceProfile::kNpu;
        return true;
    }
    if (value == "npu-cpu")
    {
        *profile = PerformanceProfile::kNpuCpu;
        return true;
    }
    if (value == "full")
    {
        *profile = PerformanceProfile::kFull;
        return true;
    }

    return false;
}

bool ParseDetectionOutputMode(const std::string& value, DetectionOutputMode* mode)
{
    if (!mode)
    {
        return false;
    }

    if (value == "metadata_only")
    {
        *mode = DetectionOutputMode::kMetadataOnly;
        return true;
    }
    if (value == "metadata_and_annotated_frame")
    {
        *mode = DetectionOutputMode::kMetadataAndAnnotatedFrame;
        return true;
    }

    return false;
}

bool IsSupportedCoreMask(uint32_t core_mask)
{
    constexpr std::array<uint32_t, 5> kSupportedMasks = {1, 2, 3, 4, 7};
    for (uint32_t supported_mask : kSupportedMasks)
    {
        if (core_mask == supported_mask)
        {
            return true;
        }
    }

    return false;
}

} // namespace camera_subsystem::extensions::detection_server
