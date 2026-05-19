#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_RKNN_MODEL_SESSION_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_RKNN_MODEL_SESSION_H

#include "detection_server/detection_types.h"

#include <string>

namespace camera_subsystem::extensions::detection_server {

class IRknnModelSession
{
  public:
    virtual ~IRknnModelSession() = default;

    virtual bool Initialize(const std::string& model_path, uint32_t core_mask,
                            std::string* error_message) = 0;
    virtual bool Run(const DetectionInputTensor& input,
                     std::vector<DetectionOutputTensor>* outputs,
                     std::string* error_message) = 0;
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;
    virtual DetectionErrorCode GetLastErrorCode() const = 0;
    virtual std::string GetLastErrorMessage() const = 0;
    virtual RknnRuntimeInfo GetRuntimeInfo() const = 0;
};

class RknnModelSession final : public IRknnModelSession
{
  public:
    RknnModelSession();
    ~RknnModelSession() override;

    bool Initialize(const std::string& model_path, uint32_t core_mask,
                    std::string* error_message) override;
    bool Run(const DetectionInputTensor& input,
             std::vector<DetectionOutputTensor>* outputs,
             std::string* error_message) override;
    void Shutdown() override;
    bool IsInitialized() const override;
    DetectionErrorCode GetLastErrorCode() const override;
    std::string GetLastErrorMessage() const override;
    RknnRuntimeInfo GetRuntimeInfo() const override;

  private:
    bool initialized_ = false;
    DetectionErrorCode last_error_code_ = DetectionErrorCode::kOk;
    std::string last_error_message_;
    RknnRuntimeInfo runtime_info_;
    std::string model_path_;

#if defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
    unsigned long long context_storage_ = 0;
#endif
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_RKNN_MODEL_SESSION_H
