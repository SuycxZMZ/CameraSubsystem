#include "detection_server/rknn_model_session.h"

#include <filesystem>
#include <utility>

#if defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
#include <rknn_api.h>
#endif

namespace camera_subsystem::extensions::detection_server {
namespace {

#if defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
rknn_core_mask ToRknnCoreMask(uint32_t core_mask)
{
    switch (core_mask)
    {
    case 1:
        return RKNN_NPU_CORE_0;
    case 2:
        return RKNN_NPU_CORE_1;
    case 3:
        return RKNN_NPU_CORE_0_1;
    case 4:
        return RKNN_NPU_CORE_2;
    case 7:
        return RKNN_NPU_CORE_0_1_2;
    default:
        return RKNN_NPU_CORE_UNDEFINED;
    }
}
#endif

std::string ModelNameFromPath(const std::string& model_path)
{
    return std::filesystem::path(model_path).stem().string();
}

} // namespace

RknnModelSession::RknnModelSession() = default;

RknnModelSession::~RknnModelSession()
{
    Shutdown();
}

bool RknnModelSession::Initialize(const std::string& model_path, uint32_t core_mask,
                                  std::string* error_message)
{
    Shutdown();

    auto fail = [&](DetectionErrorCode error_code, std::string message) -> bool
    {
        last_error_code_ = error_code;
        last_error_message_ = std::move(message);
        if (error_message)
        {
            *error_message = last_error_message_;
        }
        return false;
    };

    if (model_path.empty())
    {
        return fail(DetectionErrorCode::kInvalidConfig, "model_path must not be empty");
    }
    if (!IsSupportedCoreMask(core_mask))
    {
        return fail(DetectionErrorCode::kInvalidConfig, "unsupported core mask");
    }
    if (!std::filesystem::exists(model_path))
    {
        return fail(DetectionErrorCode::kModelLoadFailed, "model file does not exist");
    }

#if defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
    rknn_context context = 0;
    int result = rknn_init(&context, const_cast<char*>(model_path.c_str()), 0, 0, nullptr);
    if (result != RKNN_SUCC)
    {
        return fail(DetectionErrorCode::kModelLoadFailed, "rknn_init failed");
    }

    const rknn_core_mask rknn_core_mask = ToRknnCoreMask(core_mask);
    if (rknn_core_mask == RKNN_NPU_CORE_UNDEFINED ||
        rknn_set_core_mask(context, rknn_core_mask) != RKNN_SUCC)
    {
        rknn_destroy(context);
        return fail(DetectionErrorCode::kCoreMaskFailed, "rknn_set_core_mask failed");
    }

    rknn_sdk_version sdk_version;
    if (rknn_query(context, RKNN_QUERY_SDK_VERSION, &sdk_version, sizeof(sdk_version)) !=
        RKNN_SUCC)
    {
        rknn_destroy(context);
        return fail(DetectionErrorCode::kModelLoadFailed, "rknn_query sdk version failed");
    }

    context_storage_ = static_cast<unsigned long long>(context);
    runtime_info_.model_name = ModelNameFromPath(model_path);
    runtime_info_.runtime_version = sdk_version.api_version;
    runtime_info_.driver_version = sdk_version.drv_version;
    runtime_info_.npu_core_mask = core_mask;
    runtime_info_.using_stub_backend = false;
#else
    runtime_info_.model_name = ModelNameFromPath(model_path);
    runtime_info_.runtime_version = "stub-host";
    runtime_info_.driver_version = "stub-host";
    runtime_info_.npu_core_mask = core_mask;
    runtime_info_.using_stub_backend = true;
#endif

    initialized_ = true;
    last_error_code_ = DetectionErrorCode::kOk;
    last_error_message_.clear();
    if (error_message)
    {
        error_message->clear();
    }
    return true;
}

void RknnModelSession::Shutdown()
{
#if defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
    if (context_storage_ != 0)
    {
        rknn_destroy(static_cast<rknn_context>(context_storage_));
        context_storage_ = 0;
    }
#endif

    initialized_ = false;
    runtime_info_ = RknnRuntimeInfo();
}

bool RknnModelSession::IsInitialized() const
{
    return initialized_;
}

DetectionErrorCode RknnModelSession::GetLastErrorCode() const
{
    return last_error_code_;
}

std::string RknnModelSession::GetLastErrorMessage() const
{
    return last_error_message_;
}

RknnRuntimeInfo RknnModelSession::GetRuntimeInfo() const
{
    return runtime_info_;
}

} // namespace camera_subsystem::extensions::detection_server
