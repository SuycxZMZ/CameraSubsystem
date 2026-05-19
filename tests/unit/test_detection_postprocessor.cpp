#include "detection_server/detection_postprocessor.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace camera_subsystem::extensions::detection_server;

namespace {

std::filesystem::path MakeTempLabelsPath()
{
    return std::filesystem::temp_directory_path() /
           std::filesystem::path("camera_detection_labels_" + std::to_string(::getpid()) + ".txt");
}

DetectionOutputTensor MakeBoxTensor()
{
    DetectionOutputTensor tensor;
    tensor.attr.name = "box";
    tensor.attr.type = DetectionTensorDataType::kInt8;
    tensor.attr.dims = {1, 8, 1, 1};
    tensor.attr.zero_point = 0;
    tensor.attr.scale = 1.0f;
    tensor.bytes.resize(8, 0);
    auto* data = reinterpret_cast<int8_t*>(tensor.bytes.data());
    for (int i = 0; i < 4; ++i)
    {
        data[i * 2] = 0;
        data[i * 2 + 1] = 10;
    }
    return tensor;
}

DetectionOutputTensor MakeScoreTensor(int8_t score)
{
    DetectionOutputTensor tensor;
    tensor.attr.name = "score";
    tensor.attr.type = DetectionTensorDataType::kInt8;
    tensor.attr.dims = {1, 80, 1, 1};
    tensor.attr.zero_point = 0;
    tensor.attr.scale = 0.01f;
    tensor.bytes.resize(80, 0);
    reinterpret_cast<int8_t*>(tensor.bytes.data())[0] = score;
    return tensor;
}

DetectionOutputTensor MakeScoreSumTensor(int8_t score)
{
    DetectionOutputTensor tensor;
    tensor.attr.name = "score_sum";
    tensor.attr.type = DetectionTensorDataType::kInt8;
    tensor.attr.dims = {1, 1, 1, 1};
    tensor.attr.zero_point = 0;
    tensor.attr.scale = 0.01f;
    tensor.bytes.resize(1, 0);
    reinterpret_cast<int8_t*>(tensor.bytes.data())[0] = score;
    return tensor;
}

} // namespace

TEST(DetectionPostprocessorTest, ProcessProducesSingleDetection)
{
    const auto labels_path = MakeTempLabelsPath();
    {
        std::ofstream output(labels_path);
        output << "person\n";
    }

    DetectionPostprocessor postprocessor;
    std::string error_message;
    ASSERT_TRUE(postprocessor.LoadLabels(labels_path.string(), &error_message));

    RknnRuntimeInfo runtime_info;
    runtime_info.model_name = "yolo11";
    runtime_info.model_input_width = 8;
    runtime_info.model_input_height = 8;
    runtime_info.model_input_channels = 3;

    std::vector<DetectionOutputTensor> outputs;
    outputs.push_back(MakeBoxTensor());
    outputs.push_back(MakeScoreTensor(100));
    outputs.push_back(MakeScoreSumTensor(100));
    outputs.push_back(MakeBoxTensor());
    outputs.push_back(MakeScoreTensor(0));
    outputs.push_back(MakeScoreSumTensor(0));
    outputs.push_back(MakeBoxTensor());
    outputs.push_back(MakeScoreTensor(0));
    outputs.push_back(MakeScoreSumTensor(0));

    DetectionLetterboxInfo letterbox;
    letterbox.source_width = 8;
    letterbox.source_height = 8;
    letterbox.model_input_width = 8;
    letterbox.model_input_height = 8;
    letterbox.scale = 1.0f;

    std::vector<DetectionBox> boxes;
    ASSERT_TRUE(postprocessor.Process(runtime_info, outputs, letterbox, 0.25f, 0.45f, &boxes,
                                      &error_message))
        << error_message;
    ASSERT_EQ(boxes.size(), 1U);
    EXPECT_EQ(boxes[0].class_id, 0U);
    EXPECT_EQ(boxes[0].label, "person");
    EXPECT_GT(boxes[0].score, 0.25f);
    EXPECT_LE(boxes[0].x1, boxes[0].x2);
    EXPECT_LE(boxes[0].y1, boxes[0].y2);
    EXPECT_GE(boxes[0].nx1, 0.0f);
    EXPECT_LE(boxes[0].nx2, 1.0f);

    std::error_code error_code;
    std::filesystem::remove(labels_path, error_code);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
