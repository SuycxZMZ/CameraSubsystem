#include "detection_server/camera_detection_server_app.h"

#include "camera_subsystem/platform/platform_logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <utility>

namespace camera_subsystem::extensions::detection_server {
namespace {

std::atomic<bool> g_detection_stop_requested{false};

void HandleStopSignal(int)
{
    g_detection_stop_requested.store(true);
}

bool InstallStopHandlers()
{
    struct sigaction action;
    action.sa_handler = HandleStopSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    return sigaction(SIGINT, &action, nullptr) == 0 &&
           sigaction(SIGTERM, &action, nullptr) == 0;
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream stream;
    for (const char ch : value)
    {
        switch (ch)
        {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                              static_cast<unsigned char>(ch));
                stream << buffer;
            }
            else
            {
                stream << ch;
            }
            break;
        }
    }
    return stream.str();
}

bool ExtractStringField(const std::string& json, const std::string& key, std::string* value)
{
    if (!value)
    {
        return false;
    }
    const std::string needle = "\"" + key + "\":\"";
    const size_t start = json.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }

    std::string result;
    bool escape = false;
    for (size_t i = start + needle.size(); i < json.size(); ++i)
    {
        const char ch = json[i];
        if (escape)
        {
            switch (ch)
            {
            case '"':
            case '\\':
            case '/':
                result.push_back(ch);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(ch);
                break;
            }
            escape = false;
            continue;
        }

        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
        {
            *value = result;
            return true;
        }
        result.push_back(ch);
    }
    return false;
}

bool ExtractUint32Field(const std::string& json, const std::string& key, uint32_t* value)
{
    if (!value)
    {
        return false;
    }
    const std::string needle = "\"" + key + "\":";
    const size_t start = json.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }
    const char* begin = json.c_str() + start + needle.size();
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(begin, &end, 10);
    if (begin == end || errno != 0 || parsed > 0xffffffffUL)
    {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

bool ExtractDoubleField(const std::string& json, const std::string& key, double* value)
{
    if (!value)
    {
        return false;
    }
    const std::string needle = "\"" + key + "\":";
    const size_t start = json.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }
    const char* begin = json.c_str() + start + needle.size();
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(begin, &end);
    if (begin == end || errno != 0)
    {
        return false;
    }
    *value = parsed;
    return true;
}

bool ExtractBoolField(const std::string& json, const std::string& key, bool* value)
{
    if (!value)
    {
        return false;
    }
    const std::string true_needle = "\"" + key + "\":true";
    const std::string false_needle = "\"" + key + "\":false";
    if (json.find(true_needle) != std::string::npos)
    {
        *value = true;
        return true;
    }
    if (json.find(false_needle) != std::string::npos)
    {
        *value = false;
        return true;
    }
    return false;
}

std::string BuildControlResponse(const std::string& request_id,
                                 const std::string& stream_id,
                                 bool ok,
                                 const std::string& state,
                                 const std::string& error_code,
                                 const std::string& message)
{
    std::ostringstream stream;
    stream << "{"
           << "\"type\":\"detection_response\","
           << "\"request_id\":\"" << JsonEscape(request_id) << "\","
           << "\"ok\":" << (ok ? "true" : "false") << ","
           << "\"stream_id\":\"" << JsonEscape(stream_id) << "\"";
    if (!state.empty())
    {
        stream << ",\"state\":\"" << JsonEscape(state) << "\"";
    }
    if (!error_code.empty())
    {
        stream << ",\"error_code\":\"" << JsonEscape(error_code) << "\"";
    }
    if (!message.empty())
    {
        stream << ",\"message\":\"" << JsonEscape(message) << "\"";
    }
    stream << "}\n";
    return stream.str();
}

} // namespace

CameraDetectionServerApp::CameraDetectionServerApp(DetectionServerConfig config)
    : config_(std::move(config)), session_(config_),
      frame_preprocessor_(std::make_unique<MjpegFramePreprocessor>())
{
    runtime_infer_every_n_frames_.store(config_.infer_every_n_frames);
    runtime_draw_boxes_.store(config_.draw_boxes);
    runtime_score_threshold_.store(config_.score_threshold);
    runtime_nms_threshold_.store(config_.nms_threshold);
    runtime_output_mode_.store(config_.output_mode);
}

int CameraDetectionServerApp::Run()
{
    using camera_subsystem::core::LogLevel;
    using camera_subsystem::platform::PlatformLogger;

    if (!PlatformLogger::Initialize(std::string(), LogLevel::kInfo))
    {
        return 1;
    }

    std::string reason;
    if (!config_.IsValid(&reason))
    {
        PlatformLogger::Log(LogLevel::kError, "detection_server",
                            "invalid detection config: %s", reason.c_str());
        PlatformLogger::Shutdown();
        return 1;
    }

    g_detection_stop_requested.store(false);
    if (!InstallStopHandlers())
    {
        PlatformLogger::Log(LogLevel::kError, "detection_server",
                            "failed to initialize signal handler");
        PlatformLogger::Shutdown();
        return 1;
    }

    PlatformLogger::Log(LogLevel::kInfo, "detection_server",
                        "starting detection server: stream=%s model=%s core_mask=%u profile=%s "
                        "output_mode=%s draw_boxes=%u",
                        config_.stream_id.c_str(), config_.model_path.c_str(),
                        config_.npu_core_mask,
                        PerformanceProfileToString(config_.performance_profile),
                        DetectionOutputModeToString(config_.output_mode), config_.draw_boxes ? 1U
                                                                                             : 0U);
    PlatformLogger::Log(LogLevel::kInfo, "detection_server",
                        "phase 1 behavior: metadata output is backed by pluggable preprocess + "
                        "rknn session + yolo postprocess; annotated frame path is not connected yet");

    if (!control_server_.Start(
            config_.control_socket,
            [this](const std::string& request_json)
            {
                return HandleControlRequest(request_json);
            }))
    {
        PlatformLogger::Log(LogLevel::kError, "detection_server",
                            "failed to start control server: socket=%s",
                            config_.control_socket.c_str());
        PlatformLogger::Shutdown();
        return 1;
    }

    if (!StartComponents())
    {
        control_server_.Stop();
        StopComponents();
        PlatformLogger::Shutdown();
        return 1;
    }

    const auto interval = std::chrono::milliseconds(config_.metrics_interval_ms);
    auto next_log_time = std::chrono::steady_clock::now() + interval;
    while (!g_detection_stop_requested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_log_time)
        {
            LogPeriodicSummary();
            next_log_time = now + interval;
        }
    }

    PlatformLogger::Log(LogLevel::kInfo, "detection_server",
                        "stop signal received, shutting down detection server");
    LogPeriodicSummary();
    control_server_.Stop();
    StopComponents();

    PlatformLogger::Shutdown();
    return 0;
}

bool CameraDetectionServerApp::StartComponents()
{
    using camera_subsystem::core::LogLevel;
    using camera_subsystem::platform::PlatformLogger;
    std::lock_guard<std::mutex> lock(components_mutex_);

    if (components_running_.load())
    {
        return true;
    }

    ResetRuntimeStats();

    if (!session_.Start())
    {
        const DetectionSessionSnapshot snapshot = session_.GetSnapshot();
        PlatformLogger::Log(LogLevel::kError, "detection_server",
                            "failed to start detection session: code=%s message=%s",
                            DetectionErrorCodeToString(snapshot.error_code),
                            snapshot.error_message.c_str());
        return false;
    }

    std::string error_message;
    if (!postprocessor_.LoadLabels(config_.labels_path, &error_message))
    {
        PlatformLogger::Log(LogLevel::kError, "detection_server",
                            "failed to load labels: %s path=%s", error_message.c_str(),
                            config_.labels_path.c_str());
        session_.Stop();
        return false;
    }

    DetectionPublisherConfig publisher_config;
    publisher_config.result_socket = config_.result_socket;
    if (!publisher_.Start(publisher_config))
    {
        PlatformLogger::Log(LogLevel::kError, "detection_server",
                            "failed to start detection publisher: socket=%s",
                            config_.result_socket.c_str());
        session_.Stop();
        return false;
    }

    CameraFrameSubscriberConfig subscriber_config;
    subscriber_config.control_socket = config_.camera_control_socket;
    subscriber_config.data_socket = config_.camera_data_socket;
    subscriber_config.device_path = config_.device_path;
    subscriber_config.stream_id = config_.stream_id;
    subscriber_config.client_id = config_.client_id;
    subscriber_config.camera_id = config_.camera_id;
    subscriber_config.max_frame_size = config_.max_frame_size;
    subscriber_config.frame_callback = [this](
                                           const camera_subsystem::ipc::CameraDataFrameHeader& header,
                                           const std::vector<uint8_t>& payload)
    { OnFrame(header, payload); };
    if (!subscriber_.Start(subscriber_config))
    {
        PlatformLogger::Log(LogLevel::kError, "detection_server",
                            "failed to start frame subscriber: stream=%s device=%s stage=%s err=%d msg=%s",
                            config_.stream_id.c_str(), config_.device_path.c_str(),
                            subscriber_.GetLastErrorStage().c_str(),
                            subscriber_.GetLastErrorNo(),
                            subscriber_.GetLastErrorMessage().c_str());
        publisher_.Stop();
        session_.Stop();
        return false;
    }

    const DetectionSessionSnapshot snapshot = session_.GetSnapshot();
    components_running_.store(true);
    PlatformLogger::Log(
        LogLevel::kInfo, "detection_server",
        "detection server running: runtime=%s driver=%s stub=%u control_socket=%s result_socket=%s",
        snapshot.runtime_info.runtime_version.c_str(), snapshot.runtime_info.driver_version.c_str(),
        snapshot.runtime_info.using_stub_backend ? 1U : 0U, config_.control_socket.c_str(),
        config_.result_socket.c_str());
    return true;
}

void CameraDetectionServerApp::StopComponents()
{
    std::lock_guard<std::mutex> lock(components_mutex_);
    if (!components_running_.load())
    {
        return;
    }
    subscriber_.Stop();
    publisher_.Stop();
    session_.Stop();
    components_running_.store(false);
}

void CameraDetectionServerApp::ResetRuntimeStats()
{
    callback_frames_.store(0);
    callback_bytes_.store(0);
    publish_failures_.store(0);
    inferred_frames_.store(0);
    preprocess_failures_.store(0);
    inference_failures_.store(0);
    postprocess_failures_.store(0);
    last_object_count_.store(0);
    std::lock_guard<std::mutex> lock(last_frame_mutex_);
    last_frame_id_ = 0;
    last_timestamp_ns_ = 0;
    last_width_ = 0;
    last_height_ = 0;
}

void CameraDetectionServerApp::OnFrame(const camera_subsystem::ipc::CameraDataFrameHeader& header,
                                       const std::vector<uint8_t>& payload)
{
    callback_frames_.fetch_add(1);
    callback_bytes_.fetch_add(payload.size());
    {
        std::lock_guard<std::mutex> lock(last_frame_mutex_);
        last_frame_id_ = header.frame_id;
        last_timestamp_ns_ = header.timestamp_ns;
        last_width_ = header.width;
        last_height_ = header.height;
    }

    DetectionResult result;
    result.stream_id = config_.stream_id;
    result.frame_id = header.frame_id;
    result.timestamp_ns = header.timestamp_ns;
    result.image_width = header.width;
    result.image_height = header.height;

    const DetectionSessionSnapshot snapshot = session_.GetSnapshot();
    result.model_name = snapshot.runtime_info.model_name;
    result.runtime_version = snapshot.runtime_info.runtime_version;
    result.driver_version = snapshot.runtime_info.driver_version;
    result.npu_core_mask = snapshot.runtime_info.npu_core_mask;
    result.letterbox_width = snapshot.runtime_info.model_input_width;
    result.letterbox_height = snapshot.runtime_info.model_input_height;

    const uint64_t callback_frame_index = callback_frames_.load();
    const uint32_t infer_every_n_frames = runtime_infer_every_n_frames_.load();
    if (infer_every_n_frames > 1 &&
        ((callback_frame_index - 1U) % infer_every_n_frames) != 0U)
    {
        last_object_count_.store(0);
        if (!publisher_.PublishResult(result))
        {
            publish_failures_.fetch_add(1);
        }
        return;
    }

    const auto preprocess_begin = std::chrono::steady_clock::now();
    PreprocessedFrame preprocessed;
    std::string error_message;
    if (!frame_preprocessor_ ||
        !frame_preprocessor_->Preprocess(header, payload, snapshot.runtime_info, &preprocessed,
                                         &error_message))
    {
        preprocess_failures_.fetch_add(1);
        last_object_count_.store(0);
        if (!error_message.empty())
        {
            camera_subsystem::platform::PlatformLogger::Log(
                camera_subsystem::core::LogLevel::kWarning, "detection_server",
                "frame preprocess failed: frame_id=%lu reason=%s", header.frame_id,
                error_message.c_str());
        }
        if (!publisher_.PublishResult(result))
        {
            publish_failures_.fetch_add(1);
        }
        return;
    }
    const auto preprocess_end = std::chrono::steady_clock::now();

    std::vector<DetectionOutputTensor> outputs;
    if (!session_.RunInference(preprocessed.input_tensor, &outputs, &error_message))
    {
        inference_failures_.fetch_add(1);
        last_object_count_.store(0);
        camera_subsystem::platform::PlatformLogger::Log(
            camera_subsystem::core::LogLevel::kWarning, "detection_server",
            "rknn inference failed: frame_id=%lu reason=%s", header.frame_id,
            error_message.c_str());
        if (!publisher_.PublishResult(result))
        {
            publish_failures_.fetch_add(1);
        }
        return;
    }
    const auto inference_end = std::chrono::steady_clock::now();

    if (!postprocessor_.Process(snapshot.runtime_info, outputs, preprocessed.letterbox,
                                static_cast<float>(runtime_score_threshold_.load()),
                                static_cast<float>(runtime_nms_threshold_.load()), &result.objects,
                                &error_message))
    {
        postprocess_failures_.fetch_add(1);
        result.objects.clear();
        last_object_count_.store(0);
        camera_subsystem::platform::PlatformLogger::Log(
            camera_subsystem::core::LogLevel::kWarning, "detection_server",
            "postprocess failed: frame_id=%lu reason=%s", header.frame_id, error_message.c_str());
    }
    const auto postprocess_end = std::chrono::steady_clock::now();

    result.letterbox_width = preprocessed.letterbox.model_input_width;
    result.letterbox_height = preprocessed.letterbox.model_input_height;
    result.preprocess_ms =
        std::chrono::duration<double, std::milli>(preprocess_end - preprocess_begin).count();
    result.inference_ms =
        std::chrono::duration<double, std::milli>(inference_end - preprocess_end).count();
    result.postprocess_ms =
        std::chrono::duration<double, std::milli>(postprocess_end - inference_end).count();
    result.total_ms =
        std::chrono::duration<double, std::milli>(postprocess_end - preprocess_begin).count();
    inferred_frames_.fetch_add(1);
    last_object_count_.store(static_cast<uint32_t>(result.objects.size()));

    if (!publisher_.PublishResult(result))
    {
        publish_failures_.fetch_add(1);
    }
}

void CameraDetectionServerApp::LogPeriodicSummary() const
{
    using camera_subsystem::core::LogLevel;
    using camera_subsystem::platform::PlatformLogger;

    const CameraFrameSubscriberStats subscriber_stats = subscriber_.GetStats();
    const DetectionPublisherStats publisher_stats = publisher_.GetStats();
    const DetectionSessionSnapshot snapshot = session_.GetSnapshot();

    uint64_t last_frame_id = 0;
    uint64_t last_timestamp_ns = 0;
    uint32_t last_width = 0;
    uint32_t last_height = 0;
    {
        std::lock_guard<std::mutex> lock(last_frame_mutex_);
        last_frame_id = last_frame_id_;
        last_timestamp_ns = last_timestamp_ns_;
        last_width = last_width_;
        last_height = last_height_;
    }

    PlatformLogger::Log(
        LogLevel::kInfo, "detection_server",
        "summary: state=%s input_frames=%lu inferred=%lu pre_fail=%lu infer_fail=%lu "
        "post_fail=%lu input_bytes=%lu published=%lu pub_fail=%lu clients=%zu objects=%u "
        "last_frame_id=%lu last_ts_ns=%lu size=%ux%u runtime=%s stub=%u",
        DetectionStateToString(snapshot.state), subscriber_stats.input_frames,
        inferred_frames_.load(), preprocess_failures_.load(), inference_failures_.load(),
        postprocess_failures_.load(), subscriber_stats.input_bytes,
        publisher_stats.published_results,
        publish_failures_.load() + publisher_stats.publish_failures, publisher_stats.connected_clients,
        last_object_count_.load(), last_frame_id, last_timestamp_ns, last_width, last_height,
        snapshot.runtime_info.runtime_version.c_str(),
        snapshot.runtime_info.using_stub_backend ? 1U : 0U);
}

bool CameraDetectionServerApp::HandleStartDetection(std::string* error_code,
                                                    std::string* error_message)
{
    if (StartComponents())
    {
        if (error_code)
        {
            error_code->clear();
        }
        if (error_message)
        {
            error_message->clear();
        }
        return true;
    }

    const auto snapshot = session_.GetSnapshot();
    if (error_code)
    {
        *error_code = DetectionErrorCodeToString(snapshot.error_code);
    }
    if (error_message)
    {
        *error_message = snapshot.error_message;
    }
    return false;
}

bool CameraDetectionServerApp::HandleStopDetection(std::string* error_code,
                                                   std::string* error_message)
{
    StopComponents();
    if (error_code)
    {
        error_code->clear();
    }
    if (error_message)
    {
        error_message->clear();
    }
    return true;
}

bool CameraDetectionServerApp::HandleSetDetectionConfig(const std::string& request_json,
                                                        std::string* error_code,
                                                        std::string* error_message)
{
    uint32_t infer_every_n_frames = 0;
    if (ExtractUint32Field(request_json, "infer_every_n_frames", &infer_every_n_frames))
    {
        if (infer_every_n_frames == 0)
        {
            if (error_code)
            {
                *error_code = "INVALID_CONFIG";
            }
            if (error_message)
            {
                *error_message = "infer_every_n_frames must be >= 1";
            }
            return false;
        }
        runtime_infer_every_n_frames_.store(infer_every_n_frames);
    }

    bool draw_boxes = false;
    if (ExtractBoolField(request_json, "draw_boxes", &draw_boxes))
    {
        runtime_draw_boxes_.store(draw_boxes);
    }

    double score_threshold = 0.0;
    if (ExtractDoubleField(request_json, "score_threshold", &score_threshold))
    {
        if (score_threshold < 0.0 || score_threshold > 1.0)
        {
            if (error_code)
            {
                *error_code = "INVALID_CONFIG";
            }
            if (error_message)
            {
                *error_message = "score_threshold must be in [0,1]";
            }
            return false;
        }
        runtime_score_threshold_.store(score_threshold);
    }

    double nms_threshold = 0.0;
    if (ExtractDoubleField(request_json, "nms_threshold", &nms_threshold))
    {
        if (nms_threshold < 0.0 || nms_threshold > 1.0)
        {
            if (error_code)
            {
                *error_code = "INVALID_CONFIG";
            }
            if (error_message)
            {
                *error_message = "nms_threshold must be in [0,1]";
            }
            return false;
        }
        runtime_nms_threshold_.store(nms_threshold);
    }

    std::string output_mode;
    if (ExtractStringField(request_json, "output_mode", &output_mode))
    {
        DetectionOutputMode parsed_mode;
        if (!ParseDetectionOutputMode(output_mode, &parsed_mode))
        {
            if (error_code)
            {
                *error_code = "INVALID_CONFIG";
            }
            if (error_message)
            {
                *error_message = "invalid output_mode";
            }
            return false;
        }
        runtime_output_mode_.store(parsed_mode);
    }

    if (error_code)
    {
        error_code->clear();
    }
    if (error_message)
    {
        error_message->clear();
    }
    return true;
}

std::string CameraDetectionServerApp::HandleControlRequest(const std::string& request_json)
{
    std::string request_id;
    std::string type;
    std::string stream_id;
    ExtractStringField(request_json, "request_id", &request_id);
    ExtractStringField(request_json, "type", &type);
    ExtractStringField(request_json, "stream_id", &stream_id);
    if (stream_id.empty())
    {
        stream_id = config_.stream_id;
    }

    if (type == "get_detection_status")
    {
        return BuildStatusResponseJson(request_id);
    }

    std::string error_code;
    std::string error_message;
    if (type == "start_detection")
    {
        const bool ok = HandleStartDetection(&error_code, &error_message);
        return BuildControlResponse(request_id, stream_id, ok,
                                    DetectionStateToString(session_.GetSnapshot().state),
                                    error_code, error_message);
    }
    if (type == "stop_detection")
    {
        const bool ok = HandleStopDetection(&error_code, &error_message);
        return BuildControlResponse(request_id, stream_id, ok,
                                    DetectionStateToString(session_.GetSnapshot().state),
                                    error_code, error_message);
    }
    if (type == "set_detection_config")
    {
        const bool ok = HandleSetDetectionConfig(request_json, &error_code, &error_message);
        return BuildControlResponse(request_id, stream_id, ok,
                                    DetectionStateToString(session_.GetSnapshot().state),
                                    error_code, error_message);
    }

    return BuildControlResponse(request_id, stream_id, false, std::string(),
                                "INVALID_CONFIG", "unsupported request type");
}

std::string CameraDetectionServerApp::BuildStatusResponseJson(const std::string& request_id) const
{
    const DetectionSessionSnapshot snapshot = session_.GetSnapshot();
    const CameraFrameSubscriberStats subscriber_stats = subscriber_.GetStats();
    const DetectionPublisherStats publisher_stats = publisher_.GetStats();

    std::ostringstream stream;
    stream << "{"
           << "\"type\":\"detection_status\","
           << "\"request_id\":\"" << JsonEscape(request_id) << "\","
           << "\"stream_id\":\"" << JsonEscape(config_.stream_id) << "\","
           << "\"state\":\"" << DetectionStateToString(snapshot.state) << "\","
           << "\"model_name\":\"" << JsonEscape(snapshot.runtime_info.model_name) << "\","
           << "\"npu_core_mask\":" << snapshot.runtime_info.npu_core_mask << ","
           << "\"result_socket\":\"" << JsonEscape(config_.result_socket) << "\","
           << "\"performance_profile\":{"
           << "\"name\":\"" << PerformanceProfileToString(config_.performance_profile) << "\","
           << "\"applied\":" << (snapshot.performance_profile.applied ? "true" : "false") << ","
           << "\"npu_governor\":\"" << JsonEscape(snapshot.performance_profile.npu_governor)
           << "\","
           << "\"npu_cur_freq_hz\":" << snapshot.performance_profile.npu_cur_freq_hz << ","
           << "\"npu_max_freq_hz\":" << snapshot.performance_profile.npu_max_freq_hz << ","
           << "\"cpu_policy0_governor\":\""
           << JsonEscape(snapshot.performance_profile.cpu_policy0_governor) << "\","
           << "\"cpu_policy4_governor\":\""
           << JsonEscape(snapshot.performance_profile.cpu_policy4_governor) << "\"},"
           << "\"config\":{"
           << "\"infer_every_n_frames\":" << runtime_infer_every_n_frames_.load() << ","
           << "\"draw_boxes\":" << (runtime_draw_boxes_.load() ? "true" : "false") << ","
           << "\"score_threshold\":" << runtime_score_threshold_.load() << ","
           << "\"nms_threshold\":" << runtime_nms_threshold_.load() << ","
           << "\"output_mode\":\""
           << DetectionOutputModeToString(runtime_output_mode_.load()) << "\"},"
           << "\"metrics\":{"
           << "\"input_frames\":" << subscriber_stats.input_frames << ","
           << "\"inferred_frames\":" << inferred_frames_.load() << ","
           << "\"input_bytes\":" << subscriber_stats.input_bytes << ","
           << "\"preprocess_failures\":" << preprocess_failures_.load() << ","
           << "\"inference_failures\":" << inference_failures_.load() << ","
           << "\"postprocess_failures\":" << postprocess_failures_.load() << ","
           << "\"publish_failures\":"
           << (publish_failures_.load() + publisher_stats.publish_failures) << ","
           << "\"connected_result_clients\":" << publisher_stats.connected_clients << ","
           << "\"last_object_count\":" << last_object_count_.load() << "},"
           << "\"last_error\":\"" << JsonEscape(snapshot.error_message) << "\""
           << "}\n";
    return stream.str();
}

} // namespace camera_subsystem::extensions::detection_server
