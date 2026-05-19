#include "detection_server/detection_publisher.h"

#include <gtest/gtest.h>

using namespace camera_subsystem::extensions::detection_server;

TEST(DetectionPublisherTest, SerializeJsonLineContainsEscapedFields)
{
    DetectionResult result;
    result.stream_id = "usb\"0\\main";
    result.frame_id = 42;
    result.timestamp_ns = 123456789;
    result.model_name = "yolo11n";
    result.runtime_version = "2.3.2";
    result.driver_version = "0.9\nbeta";
    result.npu_core_mask = 1;
    result.image_width = 640;
    result.image_height = 480;
    result.letterbox_width = 640;
    result.letterbox_height = 640;
    result.preprocess_ms = 0.5;
    result.inference_ms = 17.0;
    result.postprocess_ms = 0.8;
    result.total_ms = 18.3;

    DetectionBox box;
    box.class_id = 1;
    box.label = "person\tlabel";
    box.score = 0.9f;
    box.x1 = 10;
    box.y1 = 20;
    box.x2 = 100;
    box.y2 = 200;
    box.nx1 = 0.1f;
    box.ny1 = 0.2f;
    box.nx2 = 0.3f;
    box.ny2 = 0.4f;
    result.objects.push_back(box);

    const std::string json = DetectionPublisher::SerializeDetectionResultJsonLine(result);
    EXPECT_NE(json.find("\"type\":\"detection_result\""), std::string::npos);
    EXPECT_NE(json.find("\"stream_id\":\"usb\\\"0\\\\main\""), std::string::npos);
    EXPECT_NE(json.find("\"driver_version\":\"0.9\\nbeta\""), std::string::npos);
    EXPECT_NE(json.find("\"label\":\"person\\tlabel\""), std::string::npos);
    EXPECT_NE(json.find("\"frame_id\":42"), std::string::npos);
    EXPECT_EQ(json.back(), '\n');
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
