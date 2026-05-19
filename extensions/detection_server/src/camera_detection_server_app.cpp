#include "detection_server/camera_detection_server_app.h"

#include "camera_subsystem/platform/platform_logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
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

} // namespace

CameraDetectionServerApp::CameraDetectionServerApp(DetectionServerConfig config)
    : config_(std::move(config)), session_(config_),
      frame_preprocessor_(std::make_unique<MjpegFramePreprocessor>())
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

    if (!StartComponents())
    {
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
    StopComponents();

    PlatformLogger::Shutdown();
    return 0;
}

bool CameraDetectionServerApp::StartComponents()
{
    using camera_subsystem::core::LogLevel;
    using camera_subsystem::platform::PlatformLogger;

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
    PlatformLogger::Log(
        LogLevel::kInfo, "detection_server",
        "detection server running: runtime=%s driver=%s stub=%u result_socket=%s",
        snapshot.runtime_info.runtime_version.c_str(), snapshot.runtime_info.driver_version.c_str(),
        snapshot.runtime_info.using_stub_backend ? 1U : 0U, config_.result_socket.c_str());
    return true;
}

void CameraDetectionServerApp::StopComponents()
{
    subscriber_.Stop();
    publisher_.Stop();
    session_.Stop();
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
    if (config_.infer_every_n_frames > 1 &&
        ((callback_frame_index - 1U) % config_.infer_every_n_frames) != 0U)
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
                                static_cast<float>(config_.score_threshold),
                                static_cast<float>(config_.nms_threshold), &result.objects,
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

} // namespace camera_subsystem::extensions::detection_server
