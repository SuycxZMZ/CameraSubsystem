#include "detection_server/rknn_model_session.h"

#include <cstring>
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

DetectionTensorDataType ToDetectionTensorType(rknn_tensor_type tensor_type)
{
    switch (tensor_type)
    {
    case RKNN_TENSOR_FLOAT32:
        return DetectionTensorDataType::kFloat32;
    case RKNN_TENSOR_INT8:
        return DetectionTensorDataType::kInt8;
    case RKNN_TENSOR_UINT8:
        return DetectionTensorDataType::kUint8;
    default:
        return DetectionTensorDataType::kUnknown;
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

    rknn_input_output_num io_num;
    if (rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)) != RKNN_SUCC)
    {
        rknn_destroy(context);
        return fail(DetectionErrorCode::kModelLoadFailed, "rknn_query io num failed");
    }

    rknn_tensor_attr input_attr;
    std::memset(&input_attr, 0, sizeof(input_attr));
    input_attr.index = 0;
    if (rknn_query(context, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr)) != RKNN_SUCC)
    {
        rknn_destroy(context);
        return fail(DetectionErrorCode::kModelLoadFailed, "rknn_query input attr failed");
    }

    context_storage_ = static_cast<unsigned long long>(context);
    model_path_ = model_path;
    runtime_info_.model_name = ModelNameFromPath(model_path);
    runtime_info_.runtime_version = sdk_version.api_version;
    runtime_info_.driver_version = sdk_version.drv_version;
    runtime_info_.npu_core_mask = core_mask;
    runtime_info_.model_input_nchw = input_attr.fmt == RKNN_TENSOR_NCHW;
    if (runtime_info_.model_input_nchw)
    {
        runtime_info_.model_input_channels = input_attr.dims[1];
        runtime_info_.model_input_height = input_attr.dims[2];
        runtime_info_.model_input_width = input_attr.dims[3];
    }
    else
    {
        runtime_info_.model_input_height = input_attr.dims[1];
        runtime_info_.model_input_width = input_attr.dims[2];
        runtime_info_.model_input_channels = input_attr.dims[3];
    }
    runtime_info_.output_tensor_attrs.clear();
    runtime_info_.output_tensor_attrs.reserve(io_num.n_output);
    for (uint32_t index = 0; index < io_num.n_output; ++index)
    {
        rknn_tensor_attr output_attr;
        std::memset(&output_attr, 0, sizeof(output_attr));
        output_attr.index = index;
        if (rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &output_attr, sizeof(output_attr)) != RKNN_SUCC)
        {
            rknn_destroy(context);
            context_storage_ = 0;
            runtime_info_ = RknnRuntimeInfo();
            return fail(DetectionErrorCode::kModelLoadFailed, "rknn_query output attr failed");
        }

        DetectionTensorAttr attr;
        if (output_attr.name)
        {
            attr.name = output_attr.name;
        }
        attr.type = ToDetectionTensorType(output_attr.type);
        attr.zero_point = output_attr.zp;
        attr.scale = output_attr.scale;
        attr.dims.reserve(output_attr.n_dims);
        for (uint32_t dim_index = 0; dim_index < output_attr.n_dims; ++dim_index)
        {
            attr.dims.push_back(output_attr.dims[dim_index]);
        }
        runtime_info_.output_tensor_attrs.push_back(std::move(attr));
    }
    runtime_info_.using_stub_backend = false;
#else
    model_path_ = model_path;
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

bool RknnModelSession::Run(const DetectionInputTensor& input,
                           std::vector<DetectionOutputTensor>* outputs,
                           std::string* error_message)
{
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

    if (!initialized_)
    {
        return fail(DetectionErrorCode::kInvalidState, "rknn session is not initialized");
    }
    if (!outputs)
    {
        return fail(DetectionErrorCode::kInvalidConfig, "outputs must not be null");
    }
    outputs->clear();

#if !defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
    (void)input;
    return fail(DetectionErrorCode::kInferenceFailed, "stub backend does not support rknn_run");
#else
    if (runtime_info_.model_input_width == 0 || runtime_info_.model_input_height == 0 ||
        runtime_info_.model_input_channels == 0)
    {
        return fail(DetectionErrorCode::kInferenceFailed, "model input shape is not available");
    }
    if (input.width != runtime_info_.model_input_width ||
        input.height != runtime_info_.model_input_height ||
        input.channels != runtime_info_.model_input_channels)
    {
        return fail(DetectionErrorCode::kInvalidConfig, "input tensor shape does not match model input");
    }
    if (input.bytes.size() !=
        static_cast<size_t>(input.width) * input.height * input.channels)
    {
        return fail(DetectionErrorCode::kInvalidConfig, "input tensor size is invalid");
    }
    const rknn_context context = static_cast<rknn_context>(context_storage_);
    if (context == 0)
    {
        return fail(DetectionErrorCode::kInvalidState, "rknn context is not available");
    }

    rknn_input input_desc;
    std::memset(&input_desc, 0, sizeof(input_desc));
    input_desc.index = 0;
    input_desc.type = RKNN_TENSOR_UINT8;
    input_desc.fmt = RKNN_TENSOR_NHWC;
    input_desc.size = input.bytes.size();
    input_desc.buf = const_cast<uint8_t*>(input.bytes.data());
    if (rknn_inputs_set(context, 1, &input_desc) != RKNN_SUCC)
    {
        return fail(DetectionErrorCode::kInferenceFailed, "rknn_inputs_set failed");
    }
    if (rknn_run(context, nullptr) != RKNN_SUCC)
    {
        return fail(DetectionErrorCode::kInferenceFailed, "rknn_run failed");
    }

    std::vector<rknn_output> rknn_outputs(runtime_info_.output_tensor_attrs.size());
    for (size_t i = 0; i < rknn_outputs.size(); ++i)
    {
        rknn_outputs[i].index = static_cast<uint32_t>(i);
        rknn_outputs[i].want_float = 0;
    }
    if (rknn_outputs_get(context, rknn_outputs.size(), rknn_outputs.data(), nullptr) != RKNN_SUCC)
    {
        return fail(DetectionErrorCode::kInferenceFailed, "rknn_outputs_get failed");
    }

    outputs->reserve(rknn_outputs.size());
    for (size_t i = 0; i < rknn_outputs.size(); ++i)
    {
        DetectionOutputTensor tensor;
        tensor.attr = runtime_info_.output_tensor_attrs[i];
        tensor.bytes.resize(rknn_outputs[i].size);
        std::memcpy(tensor.bytes.data(), rknn_outputs[i].buf, rknn_outputs[i].size);
        outputs->push_back(std::move(tensor));
    }
    rknn_outputs_release(context, rknn_outputs.size(), rknn_outputs.data());

    last_error_code_ = DetectionErrorCode::kOk;
    last_error_message_.clear();
    if (error_message)
    {
        error_message->clear();
    }
    return true;
#endif
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
    model_path_.clear();
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
