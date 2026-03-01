/**
 * @file test_camera_session_manager.cpp
 * @brief CameraSessionManager 单元测试
 * @author CameraSubsystem Team
 * @date 2026-03-01
 *
 * 测试目标：
 * 1. 验证“唯一核心发布端”注册约束与反注册约束。
 * 2. 验证按订阅引用计数驱动的 Start/Stop 回调触发时机。
 * 3. 验证子发布端与订阅端角色计数逻辑以及幂等订阅行为。
 * 4. 验证默认设备路径回退（空 path -> /dev/video0）策略。
 *
 * 测试流程：
 * 1. 使用回调计数器构造 CameraSessionManager。
 * 2. 分别执行无核心发布端订阅、正常订阅/退订、重复订阅等场景。
 * 3. 断言会话数、回调计数与路径信息是否符合预期。
 */

#include <gtest/gtest.h>

#include "camera_subsystem/camera/camera_session_manager.h"
#include "camera_subsystem/ipc/camera_channel_contract.h"

#include <string>
#include <vector>

using camera_subsystem::camera::CameraSessionManager;
using camera_subsystem::ipc::CameraBusType;
using camera_subsystem::ipc::CameraClientRole;
using camera_subsystem::ipc::CameraEndpoint;

namespace
{

CameraEndpoint MakeEndpoint(uint32_t camera_id, const char* path)
{
    return camera_subsystem::ipc::MakeCameraEndpoint(camera_id, CameraBusType::kUsb, camera_id, path);
}

} // namespace

TEST(CameraSessionManagerTest, RejectSubscribeWhenCorePublisherMissing)
{
    uint32_t start_count = 0;
    uint32_t stop_count = 0;

    CameraSessionManager manager(
        [&](const CameraEndpoint&)
        {
            ++start_count;
            return true;
        },
        [&](const CameraEndpoint&)
        {
            ++stop_count;
        });

    const CameraEndpoint endpoint = MakeEndpoint(0, "/dev/video0");

    EXPECT_FALSE(manager.Subscribe("sub_0", CameraClientRole::kSubscriber, endpoint));
    EXPECT_EQ(start_count, 0u);
    EXPECT_EQ(stop_count, 0u);
}

TEST(CameraSessionManagerTest, StartStopByReferenceCount)
{
    uint32_t start_count = 0;
    uint32_t stop_count = 0;
    std::vector<std::string> started_paths;
    std::vector<std::string> stopped_paths;

    CameraSessionManager manager(
        [&](const CameraEndpoint& endpoint)
        {
            ++start_count;
            started_paths.emplace_back(endpoint.device_path);
            return true;
        },
        [&](const CameraEndpoint& endpoint)
        {
            ++stop_count;
            stopped_paths.emplace_back(endpoint.device_path);
        });

    const CameraEndpoint endpoint = MakeEndpoint(1, "/dev/video1");

    EXPECT_TRUE(manager.RegisterCorePublisher("publisher_core_0"));
    EXPECT_TRUE(manager.Subscribe("codec_0", CameraClientRole::kSubPublisher, endpoint));
    EXPECT_TRUE(manager.Subscribe("app_0", CameraClientRole::kSubscriber, endpoint));

    EXPECT_EQ(manager.GetSubscriberCount(endpoint), 2u);
    EXPECT_EQ(start_count, 1u);
    ASSERT_EQ(started_paths.size(), 1u);
    EXPECT_EQ(started_paths[0], "/dev/video1");

    // 重复订阅不应重复计数
    EXPECT_TRUE(manager.Subscribe("app_0", CameraClientRole::kSubscriber, endpoint));
    EXPECT_EQ(manager.GetSubscriberCount(endpoint), 2u);
    EXPECT_EQ(start_count, 1u);

    // 非最后一个退订不触发 stop
    EXPECT_TRUE(manager.Unsubscribe("app_0", endpoint));
    EXPECT_EQ(manager.GetSubscriberCount(endpoint), 1u);
    EXPECT_EQ(stop_count, 0u);

    // 最后一个退订触发 stop
    EXPECT_TRUE(manager.Unsubscribe("codec_0", endpoint));
    EXPECT_EQ(manager.GetSubscriberCount(endpoint), 0u);
    EXPECT_EQ(stop_count, 1u);
    ASSERT_EQ(stopped_paths.size(), 1u);
    EXPECT_EQ(stopped_paths[0], "/dev/video1");
}

TEST(CameraSessionManagerTest, SingleCorePublisherConstraint)
{
    CameraSessionManager manager(
        [](const CameraEndpoint&)
        {
            return true;
        },
        [](const CameraEndpoint&)
        {
        });

    const CameraEndpoint endpoint = MakeEndpoint(0, "/dev/video0");

    EXPECT_TRUE(manager.RegisterCorePublisher("publisher_core_0"));
    EXPECT_FALSE(manager.RegisterCorePublisher("publisher_core_1"));

    EXPECT_TRUE(manager.Subscribe("sub_0", CameraClientRole::kSubscriber, endpoint));

    // 有活动会话时不可反注册核心发布端
    EXPECT_FALSE(manager.UnregisterCorePublisher("publisher_core_0"));

    EXPECT_TRUE(manager.Unsubscribe("sub_0", endpoint));
    EXPECT_TRUE(manager.UnregisterCorePublisher("publisher_core_0"));
    EXPECT_FALSE(manager.IsCorePublisherRegistered());
}

TEST(CameraSessionManagerTest, EmptyDevicePathFallsBackToDefaultCamera)
{
    uint32_t start_count = 0;

    CameraSessionManager manager(
        [&](const CameraEndpoint& endpoint)
        {
            ++start_count;
            EXPECT_STREQ(endpoint.device_path, CAMERA_SUBSYSTEM_DEFAULT_CAMERA);
            return true;
        },
        [](const CameraEndpoint&)
        {
        });

    CameraEndpoint endpoint = MakeEndpoint(0, "");

    EXPECT_TRUE(manager.RegisterCorePublisher("publisher_core_0"));
    EXPECT_TRUE(manager.Subscribe("sub_0", CameraClientRole::kSubscriber, endpoint));
    EXPECT_EQ(start_count, 1u);
}
