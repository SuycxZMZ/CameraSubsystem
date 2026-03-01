/**
 * @file test_camera_control_ipc.cpp
 * @brief Camera 控制面 IPC 单元测试
 * @author CameraSubsystem Team
 * @date 2026-03-01
 *
 * 测试目标：
 * 1. 验证 Unix Domain Socket 控制面请求/响应链路可用。
 * 2. 验证订阅与退订可正确驱动 CameraSessionManager 的按路启停。
 * 3. 验证客户端异常断连后，服务端可自动清理会话引用。
 *
 * 测试流程：
 * 1. 启动 CameraSessionManager，并注册唯一核心发布端。
 * 2. 启动 CameraControlServer，监听临时 socket 文件。
 * 3. 通过 CameraControlClient 发起 Subscribe/Unsubscribe/Ping 请求。
 * 4. 校验响应状态、订阅计数、start/stop 回调次数是否符合预期。
 * 5. 结束后停止服务端并删除临时 socket 文件。
 */

#include <gtest/gtest.h>

#include "camera_subsystem/camera/camera_session_manager.h"
#include "camera_subsystem/ipc/camera_control_client.h"
#include "camera_subsystem/ipc/camera_control_server.h"

#include <cerrno>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using camera_subsystem::camera::CameraSessionManager;
using camera_subsystem::ipc::CameraBusType;
using camera_subsystem::ipc::CameraClientRole;
using camera_subsystem::ipc::CameraControlClient;
using camera_subsystem::ipc::CameraControlResponse;
using camera_subsystem::ipc::CameraControlServer;
using camera_subsystem::ipc::CameraControlStatus;
using camera_subsystem::ipc::CameraEndpoint;

namespace
{

CameraEndpoint MakeEndpoint(uint32_t camera_id, const char* path)
{
    return camera_subsystem::ipc::MakeCameraEndpoint(camera_id, CameraBusType::kUsb, camera_id, path);
}

std::string MakeUniqueSocketPath()
{
    const long long now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    return "/tmp/camera_subsystem_test_" + std::to_string(getpid()) + "_" +
           std::to_string(now_ns) + ".sock";
}

bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

class CameraControlIpcFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        session_manager_ = std::make_unique<CameraSessionManager>(
            [&](const CameraEndpoint& endpoint)
            {
                std::lock_guard<std::mutex> lock(records_mutex_);
                ++start_count_;
                started_paths_.push_back(endpoint.device_path);
                return true;
            },
            [&](const CameraEndpoint& endpoint)
            {
                std::lock_guard<std::mutex> lock(records_mutex_);
                ++stop_count_;
                stopped_paths_.push_back(endpoint.device_path);
            });

        ASSERT_TRUE(session_manager_->RegisterCorePublisher("publisher_core_test"));

        socket_path_ = MakeUniqueSocketPath();
        server_ = std::make_unique<CameraControlServer>(session_manager_.get());

        if (!server_->Start(socket_path_))
        {
            const int error_no = server_->GetLastErrorNo();
            const std::string error_stage = server_->GetLastErrorStage();
            const std::string error_message = server_->GetLastErrorMessage();

            const bool is_sandbox_bind_restriction =
                error_stage == "bind" &&
                (error_no == EPERM || error_no == EACCES);

            if (is_sandbox_bind_restriction)
            {
                GTEST_SKIP() << "Skip because unix socket bind is denied by environment. "
                             << "stage=" << error_stage << ", errno=" << error_no
                             << ", message=" << error_message
                             << ", socket_path=" << socket_path_;
            }

            FAIL() << "CameraControlServer::Start failed unexpectedly. "
                   << "stage=" << error_stage
                   << ", errno=" << error_no
                   << ", message=" << error_message
                   << ", socket_path=" << socket_path_;
        }
    }

    void TearDown() override
    {
        if (server_)
        {
            server_->Stop();
        }
        if (!socket_path_.empty())
        {
            unlink(socket_path_.c_str());
        }
    }

    std::unique_ptr<CameraSessionManager> session_manager_;
    std::unique_ptr<CameraControlServer> server_;
    std::string socket_path_;

    std::mutex records_mutex_;
    uint32_t start_count_ = 0;
    uint32_t stop_count_ = 0;
    std::vector<std::string> started_paths_;
    std::vector<std::string> stopped_paths_;
};

} // namespace

TEST_F(CameraControlIpcFixture, SubscribeAndUnsubscribe)
{
    CameraControlClient client;
    ASSERT_TRUE(client.Connect(socket_path_));

    const CameraEndpoint endpoint = MakeEndpoint(0, "/dev/video0");
    CameraControlResponse response;

    EXPECT_TRUE(client.Subscribe("app_0", CameraClientRole::kSubscriber, endpoint, &response));
    EXPECT_EQ(response.status, CameraControlStatus::kOk);
    EXPECT_EQ(response.active_subscriber_count, 1u);

    EXPECT_TRUE(client.Subscribe("app_0", CameraClientRole::kSubscriber, endpoint, &response));
    EXPECT_EQ(response.active_subscriber_count, 1u);

    EXPECT_TRUE(client.Unsubscribe("app_0", CameraClientRole::kSubscriber, endpoint, &response));
    EXPECT_EQ(response.status, CameraControlStatus::kOk);
    EXPECT_EQ(response.active_subscriber_count, 0u);

    {
        std::lock_guard<std::mutex> lock(records_mutex_);
        ASSERT_EQ(started_paths_.size(), 1u);
        ASSERT_EQ(stopped_paths_.size(), 1u);
        EXPECT_EQ(start_count_, 1u);
        EXPECT_EQ(stop_count_, 1u);
        EXPECT_EQ(started_paths_[0], "/dev/video0");
        EXPECT_EQ(stopped_paths_[0], "/dev/video0");
    }
}

TEST_F(CameraControlIpcFixture, DisconnectTriggersAutoCleanup)
{
    const CameraEndpoint endpoint = MakeEndpoint(1, "/dev/video1");
    CameraControlResponse response;

    CameraControlClient client_a;
    CameraControlClient client_b;
    ASSERT_TRUE(client_a.Connect(socket_path_));
    ASSERT_TRUE(client_b.Connect(socket_path_));

    EXPECT_TRUE(client_a.Subscribe("codec_0", CameraClientRole::kSubPublisher, endpoint, &response));
    EXPECT_TRUE(client_b.Subscribe("app_0", CameraClientRole::kSubscriber, endpoint, &response));

    client_a.Disconnect();
    EXPECT_TRUE(WaitUntil([&]() { return session_manager_->GetSubscriberCount(endpoint) == 1u; },
                          std::chrono::milliseconds(500)));

    client_b.Disconnect();
    EXPECT_TRUE(WaitUntil([&]() { return session_manager_->GetSubscriberCount(endpoint) == 0u; },
                          std::chrono::milliseconds(1000)));

    EXPECT_TRUE(WaitUntil(
        [&]()
        {
            std::lock_guard<std::mutex> lock(records_mutex_);
            return stop_count_ >= 1;
        },
        std::chrono::milliseconds(1000)));

    {
        std::lock_guard<std::mutex> lock(records_mutex_);
        EXPECT_EQ(start_count_, 1u);
        EXPECT_EQ(stop_count_, 1u);
        ASSERT_EQ(stopped_paths_.size(), 1u);
        EXPECT_EQ(stopped_paths_[0], "/dev/video1");
    }
}

TEST_F(CameraControlIpcFixture, RejectCorePublisherRoleFromClient)
{
    CameraControlClient client;
    ASSERT_TRUE(client.Connect(socket_path_));

    const CameraEndpoint endpoint = MakeEndpoint(2, "/dev/video2");
    CameraControlResponse response;

    EXPECT_FALSE(client.Subscribe("invalid_client", CameraClientRole::kCorePublisher, endpoint, &response));
    EXPECT_EQ(response.status, CameraControlStatus::kInvalidRole);
}

TEST_F(CameraControlIpcFixture, Ping)
{
    CameraControlClient client;
    ASSERT_TRUE(client.Connect(socket_path_));

    CameraControlResponse response;
    EXPECT_TRUE(client.Ping(&response));
    EXPECT_EQ(response.status, CameraControlStatus::kOk);
}
