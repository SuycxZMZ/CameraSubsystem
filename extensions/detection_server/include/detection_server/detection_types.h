#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_TYPES_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace camera_subsystem::extensions::detection_server {

enum class DetectionState : uint32_t
{
    kIdle = 0,
    kStarting = 1,
    kRunning = 2,
    kDegraded = 3,
    kError = 4,
    kStopping = 5,
};

enum class DetectionErrorCode : uint32_t
{
    kOk = 0,
    kInvalidConfig = 1,
    kPerformanceProfileFailed = 2,
    kModelLoadFailed = 3,
    kCoreMaskFailed = 4,
    kSubscribeFailed = 5,
    kFrameDecodeFailed = 6,
    kInferenceFailed = 7,
    kPublishFailed = 8,
    kInvalidState = 9,
};

enum class PerformanceProfile : uint32_t
{
    kNone = 0,
    kNpu = 1,
    kNpuCpu = 2,
    kFull = 3,
};

enum class DetectionOutputMode : uint32_t
{
    kMetadataOnly = 0,
    kMetadataAndAnnotatedFrame = 1,
};

enum class DetectionTensorDataType : uint32_t
{
    kUnknown = 0,
    kFloat32 = 1,
    kInt8 = 2,
    kUint8 = 3,
};

struct DetectionBox
{
    uint32_t class_id = 0;
    std::string label;
    float score = 0.0f;
    uint32_t x1 = 0;
    uint32_t y1 = 0;
    uint32_t x2 = 0;
    uint32_t y2 = 0;
    float nx1 = 0.0f;
    float ny1 = 0.0f;
    float nx2 = 0.0f;
    float ny2 = 0.0f;
};

struct DetectionResult
{
    std::string stream_id;
    uint64_t frame_id = 0;
    uint64_t timestamp_ns = 0;
    std::string model_name;
    std::string runtime_version;
    std::string driver_version;
    uint32_t npu_core_mask = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    uint32_t letterbox_width = 0;
    uint32_t letterbox_height = 0;
    double preprocess_ms = 0.0;
    double inference_ms = 0.0;
    double postprocess_ms = 0.0;
    double total_ms = 0.0;
    std::vector<DetectionBox> objects;
};

struct DetectionTensorAttr
{
    std::string name;
    DetectionTensorDataType type = DetectionTensorDataType::kUnknown;
    std::vector<uint32_t> dims;
    int32_t zero_point = 0;
    float scale = 1.0f;
};

struct DetectionOutputTensor
{
    DetectionTensorAttr attr;
    std::vector<uint8_t> bytes;
};

struct DetectionInputTensor
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    std::vector<uint8_t> bytes;
};

struct DetectionLetterboxInfo
{
    uint32_t source_width = 0;
    uint32_t source_height = 0;
    uint32_t model_input_width = 0;
    uint32_t model_input_height = 0;
    float scale = 1.0f;
    float x_pad = 0.0f;
    float y_pad = 0.0f;
};

struct AnnotatedFrame
{
    std::string stream_id;
    uint64_t frame_id = 0;
    uint64_t timestamp_ns = 0;
    std::string encoding;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> payload;
};

struct PerformanceProfileSnapshot
{
    PerformanceProfile profile = PerformanceProfile::kNone;
    bool applied = false;
    std::string npu_governor;
    uint64_t npu_cur_freq_hz = 0;
    uint64_t npu_max_freq_hz = 0;
    std::string cpu_policy0_governor;
    std::string cpu_policy4_governor;
    DetectionErrorCode error_code = DetectionErrorCode::kOk;
    std::string message;
};

struct RknnRuntimeInfo
{
    std::string model_name;
    std::string runtime_version;
    std::string driver_version;
    uint32_t npu_core_mask = 0;
    uint32_t model_input_width = 0;
    uint32_t model_input_height = 0;
    uint32_t model_input_channels = 0;
    bool model_input_nchw = false;
    std::vector<DetectionTensorAttr> output_tensor_attrs;
    bool using_stub_backend = false;
};

struct DetectionMetricsSnapshot
{
    DetectionState state = DetectionState::kIdle;
    uint64_t input_frames = 0;
    uint64_t inferred_frames = 0;
    uint64_t dropped_frames = 0;
    uint64_t decode_failures = 0;
    uint64_t publish_failures = 0;
    double input_fps = 0.0;
    double infer_fps = 0.0;
    double latency_avg_ms = 0.0;
    double latency_p95_ms = 0.0;
    uint32_t last_object_count = 0;
    uint32_t npu_core_mask = 0;
    std::string npu_governor;
    uint64_t npu_cur_freq_hz = 0;
    bool performance_profile_applied = false;
    std::string last_error;
};

struct DetectionSessionSnapshot
{
    DetectionState state = DetectionState::kIdle;
    DetectionErrorCode error_code = DetectionErrorCode::kOk;
    std::string error_message;
    PerformanceProfileSnapshot performance_profile;
    RknnRuntimeInfo runtime_info;
};

const char* DetectionStateToString(DetectionState state);
const char* DetectionErrorCodeToString(DetectionErrorCode error_code);
const char* PerformanceProfileToString(PerformanceProfile profile);
const char* DetectionOutputModeToString(DetectionOutputMode mode);
const char* DetectionTensorDataTypeToString(DetectionTensorDataType type);

bool ParsePerformanceProfile(const std::string& value, PerformanceProfile* profile);
bool ParseDetectionOutputMode(const std::string& value, DetectionOutputMode* mode);
bool IsSupportedCoreMask(uint32_t core_mask);

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_TYPES_H
