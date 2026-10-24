#include "detection_server/detection_session.h"

#include <utility>

namespace camera_subsystem::extensions::detection_server {

DetectionSession::DetectionSession(DetectionServerConfig config,
                                   std::unique_ptr<IRknnModelSession> model_session,
                                   std::shared_ptr<const IPerformanceProfileManager> profile_manager)
    : config_(std::move(config)), model_session_(std::move(model_session)),
      profile_manager_(std::move(profile_manager))
{
    snapshot_.state = DetectionState::kIdle;
}

DetectionSession::DetectionSession(DetectionServerConfig config, std::string sysfs_root)
    : DetectionSession(std::move(config), std::make_unique<RknnModelSession>(),
                       std::make_shared<PerformanceProfileManager>(std::move(sysfs_root)))
{
}

bool DetectionSession::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != DetectionState::kIdle)
    {
        SetErrorLocked(DetectionErrorCode::kInvalidState, "session is not idle");
        return false;
    }

    snapshot_ = DetectionSessionSnapshot();
    snapshot_.state = DetectionState::kStarting;

    PerformanceProfileSnapshot profile_snapshot;
    const bool profile_ok =
        profile_manager_->ApplyProfile(config_.performance_profile, &profile_snapshot);
    snapshot_.performance_profile = profile_snapshot;
    if (!profile_ok && !config_.allow_performance_profile_failure)
    {
        snapshot_.state = DetectionState::kError;
        SetErrorLocked(DetectionErrorCode::kPerformanceProfileFailed,
                       profile_snapshot.message.empty() ? "performance profile failed"
                                                        : profile_snapshot.message);
        return false;
    }

    std::string model_error;
    if (!model_session_->Initialize(config_.model_path, config_.npu_core_mask, &model_error))
    {
        snapshot_.state = DetectionState::kError;
        SetErrorLocked(model_session_->GetLastErrorCode(),
                       model_error.empty() ? model_session_->GetLastErrorMessage() : model_error);
        return false;
    }

    snapshot_.runtime_info = model_session_->GetRuntimeInfo();
    snapshot_.state = DetectionState::kRunning;
    snapshot_.error_code = profile_ok ? DetectionErrorCode::kOk
                                      : DetectionErrorCode::kPerformanceProfileFailed;
    snapshot_.error_message = profile_ok ? std::string() : profile_snapshot.message;
    return true;
}

bool DetectionSession::Stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state == DetectionState::kIdle)
    {
        return true;
    }

    snapshot_.state = DetectionState::kStopping;
    model_session_->Shutdown();
    const PerformanceProfileSnapshot profile_snapshot = snapshot_.performance_profile;

    snapshot_ = DetectionSessionSnapshot();
    snapshot_.state = DetectionState::kIdle;
    snapshot_.performance_profile = profile_snapshot;
    return true;
}

bool DetectionSession::RunInference(const DetectionInputTensor& input,
                                    std::vector<DetectionOutputTensor>* outputs,
                                    std::string* error_message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != DetectionState::kRunning)
    {
        SetErrorLocked(DetectionErrorCode::kInvalidState, "session is not running");
        if (error_message)
        {
            *error_message = snapshot_.error_message;
        }
        return false;
    }

    if (!model_session_->Run(input, outputs, error_message))
    {
        SetErrorLocked(model_session_->GetLastErrorCode(), model_session_->GetLastErrorMessage());
        snapshot_.state = DetectionState::kError;
        return false;
    }
    return true;
}

DetectionState DetectionSession::GetState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.state;
}

DetectionSessionSnapshot DetectionSession::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void DetectionSession::SetErrorLocked(DetectionErrorCode error_code, std::string error_message)
{
    snapshot_.error_code = error_code;
    snapshot_.error_message = std::move(error_message);
}

} // namespace camera_subsystem::extensions::detection_server
