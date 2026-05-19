#include "detection_server/rknn_model_session.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace camera_subsystem::extensions::detection_server;

namespace {

std::filesystem::path MakeTempFilePath()
{
    return std::filesystem::temp_directory_path() /
           std::filesystem::path("camera_detection_model_" + std::to_string(::getpid()) + "_" +
                                 std::to_string(::time(nullptr)) + ".rknn");
}

} // namespace

TEST(RknnModelSessionTest, MissingModelFails)
{
    RknnModelSession session;
    std::string error_message;

    EXPECT_FALSE(session.Initialize("/tmp/definitely_missing_model.rknn", 1, &error_message));
    EXPECT_EQ(session.GetLastErrorCode(), DetectionErrorCode::kModelLoadFailed);
    EXPECT_FALSE(error_message.empty());
}

TEST(RknnModelSessionTest, HostStubInitializesExistingModelPath)
{
    const auto model_path = MakeTempFilePath();
    {
        std::ofstream output(model_path);
        output << "stub";
    }

    RknnModelSession session;
    std::string error_message;
    ASSERT_TRUE(session.Initialize(model_path.string(), 1, &error_message));
    EXPECT_TRUE(error_message.empty());
    EXPECT_TRUE(session.IsInitialized());

    const RknnRuntimeInfo runtime_info = session.GetRuntimeInfo();
    EXPECT_EQ(runtime_info.model_name, model_path.stem().string());
    EXPECT_EQ(runtime_info.npu_core_mask, 1U);
#if defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
    EXPECT_FALSE(runtime_info.runtime_version.empty());
    EXPECT_FALSE(runtime_info.driver_version.empty());
    EXPECT_GT(runtime_info.model_input_width, 0U);
    EXPECT_GT(runtime_info.model_input_height, 0U);
#else
    EXPECT_TRUE(runtime_info.using_stub_backend);
    EXPECT_EQ(runtime_info.runtime_version, "stub-host");
    EXPECT_EQ(runtime_info.driver_version, "stub-host");
    EXPECT_EQ(runtime_info.model_input_width, 0U);
    EXPECT_EQ(runtime_info.model_input_height, 0U);
#endif

    session.Shutdown();
    EXPECT_FALSE(session.IsInitialized());

    std::error_code error_code;
    std::filesystem::remove(model_path, error_code);
}

TEST(RknnModelSessionTest, HostStubRunFailsGracefully)
{
    const auto model_path = MakeTempFilePath();
    {
        std::ofstream output(model_path);
        output << "stub";
    }

    RknnModelSession session;
    std::string error_message;
    ASSERT_TRUE(session.Initialize(model_path.string(), 1, &error_message));

    DetectionInputTensor input;
    input.width = 640;
    input.height = 640;
    input.channels = 3;
    input.bytes.resize(static_cast<size_t>(input.width) * input.height * input.channels, 0);
    std::vector<DetectionOutputTensor> outputs;

#if defined(DETECTION_SERVER_ENABLE_RKNN_RUNTIME)
    (void)outputs;
    SUCCEED();
#else
    EXPECT_FALSE(session.Run(input, &outputs, &error_message));
    EXPECT_EQ(session.GetLastErrorCode(), DetectionErrorCode::kInferenceFailed);
    EXPECT_NE(error_message.find("stub backend"), std::string::npos);
#endif

    std::error_code error_code;
    std::filesystem::remove(model_path, error_code);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
