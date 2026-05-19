#include "detection_server/detection_postprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <utility>

namespace camera_subsystem::extensions::detection_server {
namespace {

constexpr size_t kYoloClassCount = 80;
constexpr size_t kYoloBranchCount = 3;
constexpr size_t kMaxObjects = 128;

struct CandidateBox
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float score = 0.0f;
    int class_id = -1;
};

float ClampFloat(float value, float min_value, float max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

float DequantizeInt8(int8_t value, int32_t zero_point, float scale)
{
    return (static_cast<float>(value) - static_cast<float>(zero_point)) * scale;
}

float DequantizeUInt8(uint8_t value, int32_t zero_point, float scale)
{
    return (static_cast<float>(value) - static_cast<float>(zero_point)) * scale;
}

float ComputeIoU(const CandidateBox& lhs, const CandidateBox& rhs)
{
    const float lhs_x2 = lhs.x + lhs.w;
    const float lhs_y2 = lhs.y + lhs.h;
    const float rhs_x2 = rhs.x + rhs.w;
    const float rhs_y2 = rhs.y + rhs.h;

    const float inter_x1 = std::max(lhs.x, rhs.x);
    const float inter_y1 = std::max(lhs.y, rhs.y);
    const float inter_x2 = std::min(lhs_x2, rhs_x2);
    const float inter_y2 = std::min(lhs_y2, rhs_y2);
    const float inter_w = std::max(0.0f, inter_x2 - inter_x1);
    const float inter_h = std::max(0.0f, inter_y2 - inter_y1);
    const float inter_area = inter_w * inter_h;
    const float union_area = lhs.w * lhs.h + rhs.w * rhs.h - inter_area;
    return union_area > 0.0f ? inter_area / union_area : 0.0f;
}

bool IsTensorShapeSupported(const DetectionTensorAttr& attr)
{
    return attr.dims.size() == 4 && attr.dims[0] == 1;
}

bool TensorElementCountMatches(const DetectionOutputTensor& tensor)
{
    size_t expected = 1;
    for (uint32_t dim : tensor.attr.dims)
    {
        expected *= dim;
    }

    size_t actual = 0;
    switch (tensor.attr.type)
    {
    case DetectionTensorDataType::kFloat32:
        actual = tensor.bytes.size() / sizeof(float);
        break;
    case DetectionTensorDataType::kInt8:
    case DetectionTensorDataType::kUint8:
        actual = tensor.bytes.size();
        break;
    default:
        return false;
    }
    return expected == actual;
}

float ComputeDflValue(const DetectionOutputTensor& tensor, size_t cell_index, size_t grid_len,
                      size_t offset_in_coord, size_t dfl_len)
{
    std::vector<float> values(dfl_len, 0.0f);
    float exp_sum = 0.0f;
    for (size_t i = 0; i < dfl_len; ++i)
    {
        const size_t index = cell_index + (offset_in_coord * dfl_len + i) * grid_len;
        float value = 0.0f;
        if (tensor.attr.type == DetectionTensorDataType::kFloat32)
        {
            value = reinterpret_cast<const float*>(tensor.bytes.data())[index];
        }
        else if (tensor.attr.type == DetectionTensorDataType::kInt8)
        {
            value = DequantizeInt8(reinterpret_cast<const int8_t*>(tensor.bytes.data())[index],
                                   tensor.attr.zero_point, tensor.attr.scale);
        }
        else
        {
            value = DequantizeUInt8(reinterpret_cast<const uint8_t*>(tensor.bytes.data())[index],
                                    tensor.attr.zero_point, tensor.attr.scale);
        }
        values[i] = std::exp(value);
        exp_sum += values[i];
    }

    if (exp_sum <= 0.0f)
    {
        return 0.0f;
    }

    float acc = 0.0f;
    for (size_t i = 0; i < dfl_len; ++i)
    {
        acc += values[i] / exp_sum * static_cast<float>(i);
    }
    return acc;
}

float ReadTensorScore(const DetectionOutputTensor& tensor, size_t index)
{
    switch (tensor.attr.type)
    {
    case DetectionTensorDataType::kFloat32:
        return reinterpret_cast<const float*>(tensor.bytes.data())[index];
    case DetectionTensorDataType::kInt8:
        return DequantizeInt8(reinterpret_cast<const int8_t*>(tensor.bytes.data())[index],
                              tensor.attr.zero_point, tensor.attr.scale);
    case DetectionTensorDataType::kUint8:
        return DequantizeUInt8(reinterpret_cast<const uint8_t*>(tensor.bytes.data())[index],
                               tensor.attr.zero_point, tensor.attr.scale);
    default:
        return 0.0f;
    }
}

bool AppendBranchCandidates(const DetectionOutputTensor& box_tensor,
                            const DetectionOutputTensor& score_tensor,
                            const DetectionOutputTensor* score_sum_tensor,
                            uint32_t model_input_height,
                            float score_threshold,
                            std::vector<CandidateBox>* candidates)
{
    if (!IsTensorShapeSupported(box_tensor.attr) || !IsTensorShapeSupported(score_tensor.attr) ||
        !TensorElementCountMatches(box_tensor) || !TensorElementCountMatches(score_tensor))
    {
        return false;
    }
    if (score_sum_tensor &&
        (!IsTensorShapeSupported(score_sum_tensor->attr) || !TensorElementCountMatches(*score_sum_tensor)))
    {
        return false;
    }

    const uint32_t grid_h = box_tensor.attr.dims[2];
    const uint32_t grid_w = box_tensor.attr.dims[3];
    if (grid_h == 0 || grid_w == 0)
    {
        return false;
    }

    const size_t box_channels = box_tensor.attr.dims[1];
    if (box_channels % 4 != 0)
    {
        return false;
    }
    const size_t dfl_len = box_channels / 4U;
    const size_t grid_len = static_cast<size_t>(grid_h) * static_cast<size_t>(grid_w);
    const uint32_t stride = model_input_height / grid_h;

    for (uint32_t row = 0; row < grid_h; ++row)
    {
        for (uint32_t col = 0; col < grid_w; ++col)
        {
            const size_t cell = static_cast<size_t>(row) * grid_w + col;
            if (score_sum_tensor && ReadTensorScore(*score_sum_tensor, cell) < score_threshold)
            {
                continue;
            }

            float max_score = 0.0f;
            int max_class_id = -1;
            for (size_t class_id = 0; class_id < kYoloClassCount; ++class_id)
            {
                const size_t score_index = cell + class_id * grid_len;
                const float score = ReadTensorScore(score_tensor, score_index);
                if (score > score_threshold && score > max_score)
                {
                    max_score = score;
                    max_class_id = static_cast<int>(class_id);
                }
            }

            if (max_class_id < 0)
            {
                continue;
            }

            const float left = ComputeDflValue(box_tensor, cell, grid_len, 0, dfl_len);
            const float top = ComputeDflValue(box_tensor, cell, grid_len, 1, dfl_len);
            const float right = ComputeDflValue(box_tensor, cell, grid_len, 2, dfl_len);
            const float bottom = ComputeDflValue(box_tensor, cell, grid_len, 3, dfl_len);

            CandidateBox candidate;
            candidate.x = (-left + static_cast<float>(col) + 0.5f) * stride;
            candidate.y = (-top + static_cast<float>(row) + 0.5f) * stride;
            const float x2 = (right + static_cast<float>(col) + 0.5f) * stride;
            const float y2 = (bottom + static_cast<float>(row) + 0.5f) * stride;
            candidate.w = x2 - candidate.x;
            candidate.h = y2 - candidate.y;
            candidate.score = max_score;
            candidate.class_id = max_class_id;
            candidates->push_back(candidate);
        }
    }

    return true;
}

} // namespace

bool DetectionPostprocessor::LoadLabels(const std::string& labels_path, std::string* error_message)
{
    labels_.clear();

    std::ifstream input(labels_path);
    if (!input.is_open())
    {
        if (error_message)
        {
            *error_message = "failed to open labels file";
        }
        return false;
    }

    std::string line;
    while (std::getline(input, line))
    {
        labels_.push_back(line);
    }
    if (error_message)
    {
        error_message->clear();
    }
    return true;
}

bool DetectionPostprocessor::Process(const RknnRuntimeInfo& runtime_info,
                                     const std::vector<DetectionOutputTensor>& outputs,
                                     const DetectionLetterboxInfo& letterbox,
                                     float score_threshold,
                                     float nms_threshold,
                                     std::vector<DetectionBox>* boxes,
                                     std::string* error_message) const
{
    if (!boxes)
    {
        if (error_message)
        {
            *error_message = "boxes must not be null";
        }
        return false;
    }
    boxes->clear();

    if (runtime_info.model_input_width == 0 || runtime_info.model_input_height == 0)
    {
        if (error_message)
        {
            *error_message = "runtime_info missing model input size";
        }
        return false;
    }
    if (outputs.size() < kYoloBranchCount * 2U)
    {
        if (error_message)
        {
            *error_message = "insufficient yolo output tensors";
        }
        return false;
    }

    const size_t output_per_branch = outputs.size() / kYoloBranchCount;
    if (output_per_branch != 2U && output_per_branch != 3U)
    {
        if (error_message)
        {
            *error_message = "unsupported yolo branch output count";
        }
        return false;
    }

    std::vector<CandidateBox> candidates;
    for (size_t branch = 0; branch < kYoloBranchCount; ++branch)
    {
        const DetectionOutputTensor& box_tensor = outputs[branch * output_per_branch];
        const DetectionOutputTensor& score_tensor = outputs[branch * output_per_branch + 1U];
        const DetectionOutputTensor* score_sum_tensor =
            output_per_branch == 3U ? &outputs[branch * output_per_branch + 2U] : nullptr;
        if (!AppendBranchCandidates(box_tensor, score_tensor, score_sum_tensor,
                                    runtime_info.model_input_height, score_threshold, &candidates))
        {
            if (error_message)
            {
                *error_message = "failed to parse yolo branch tensors";
            }
            return false;
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const CandidateBox& lhs, const CandidateBox& rhs) { return lhs.score > rhs.score; });

    std::vector<bool> suppressed(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (suppressed[i])
        {
            continue;
        }
        for (size_t j = i + 1; j < candidates.size(); ++j)
        {
            if (suppressed[j] || candidates[i].class_id != candidates[j].class_id)
            {
                continue;
            }
            if (ComputeIoU(candidates[i], candidates[j]) > nms_threshold)
            {
                suppressed[j] = true;
            }
        }
    }

    const float scale = letterbox.scale > 0.0f ? letterbox.scale : 1.0f;
    const uint32_t source_width =
        letterbox.source_width > 0 ? letterbox.source_width : runtime_info.model_input_width;
    const uint32_t source_height =
        letterbox.source_height > 0 ? letterbox.source_height : runtime_info.model_input_height;

    for (size_t i = 0; i < candidates.size() && boxes->size() < kMaxObjects; ++i)
    {
        if (suppressed[i])
        {
            continue;
        }

        const CandidateBox& candidate = candidates[i];
        const float x1 = (candidate.x - letterbox.x_pad) / scale;
        const float y1 = (candidate.y - letterbox.y_pad) / scale;
        const float x2 = (candidate.x + candidate.w - letterbox.x_pad) / scale;
        const float y2 = (candidate.y + candidate.h - letterbox.y_pad) / scale;

        DetectionBox box;
        box.class_id = static_cast<uint32_t>(candidate.class_id);
        box.label = candidate.class_id >= 0 &&
                            static_cast<size_t>(candidate.class_id) < labels_.size() &&
                            !labels_[candidate.class_id].empty()
                        ? labels_[candidate.class_id]
                        : ("class_" + std::to_string(candidate.class_id));
        box.score = candidate.score;
        box.x1 = static_cast<uint32_t>(ClampFloat(x1, 0.0f, static_cast<float>(source_width)));
        box.y1 = static_cast<uint32_t>(ClampFloat(y1, 0.0f, static_cast<float>(source_height)));
        box.x2 = static_cast<uint32_t>(ClampFloat(x2, 0.0f, static_cast<float>(source_width)));
        box.y2 = static_cast<uint32_t>(ClampFloat(y2, 0.0f, static_cast<float>(source_height)));
        if (source_width > 0 && source_height > 0)
        {
            box.nx1 = static_cast<float>(box.x1) / static_cast<float>(source_width);
            box.ny1 = static_cast<float>(box.y1) / static_cast<float>(source_height);
            box.nx2 = static_cast<float>(box.x2) / static_cast<float>(source_width);
            box.ny2 = static_cast<float>(box.y2) / static_cast<float>(source_height);
        }
        boxes->push_back(std::move(box));
    }

    if (error_message)
    {
        error_message->clear();
    }
    return true;
}

} // namespace camera_subsystem::extensions::detection_server
