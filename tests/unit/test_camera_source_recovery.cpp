/**
 * @file test_camera_source_recovery.cpp
 * @brief CameraSource 断连检测与恢复单元测试
 * @author CameraSubsystem Team
 * @date 2026-05-10
 *
 * 测试目标：
 * 1. 验证 CameraConfig 恢复字段默认值与结构大小保持。
 * 2. 验证 StreamMetrics 新增断连/恢复字段。
 * 3. 验证 CameraSource 状态查询接口（不依赖 V4L2 设备）。
 * 4. 验证自动恢复默认关闭，Start/Stop 状态转换正确。
 */

#include "camera_subsystem/camera/camera_source.h"
#include "camera_subsystem/core/camera_config.h"
#include "camera_subsystem/core/metrics.h"
#include <gtest/gtest.h>

using namespace camera_subsystem;
using namespace camera_subsystem::camera;
using namespace camera_subsystem::core;

/**
 * @brief 测试 CameraConfig 恢复字段默认值
 */
TEST(CameraSourceRecoveryConfigTest, DefaultValues)
{
    CameraConfig config;

    EXPECT_FALSE(config.enable_auto_recovery);
    EXPECT_EQ(config.disconnect_threshold, 3u);
    EXPECT_EQ(config.max_recovery_attempts, 10u);
    EXPECT_EQ(config.recovery_backoff_base_ms, 1000u);
    EXPECT_EQ(config.recovery_backoff_max_ms, 30000u);
}

/**
 * @brief 测试 CameraConfig 结构大小保持 ABI 兼容
 */
TEST(CameraSourceRecoveryConfigTest, StructSizeUnchanged)
{
    EXPECT_EQ(sizeof(CameraConfig), 88u);
}

/**
 * @brief 测试 CameraConfig 恢复字段可修改
 */
TEST(CameraSourceRecoveryConfigTest, ModifyRecoveryFields)
{
    CameraConfig config;
    config.enable_auto_recovery = true;
    config.disconnect_threshold = 5;
    config.max_recovery_attempts = 20;
    config.recovery_backoff_base_ms = 500;
    config.recovery_backoff_max_ms = 60000;

    EXPECT_TRUE(config.enable_auto_recovery);
    EXPECT_EQ(config.disconnect_threshold, 5u);
    EXPECT_EQ(config.max_recovery_attempts, 20u);
    EXPECT_EQ(config.recovery_backoff_base_ms, 500u);
    EXPECT_EQ(config.recovery_backoff_max_ms, 60000u);
}

/**
 * @brief 测试 Reset 会恢复所有恢复策略字段默认值
 */
TEST(CameraSourceRecoveryConfigTest, ResetRecoveryFields)
{
    CameraConfig config = CameraConfig::GetDefault();
    config.enable_auto_recovery = true;
    config.disconnect_threshold = 9;
    config.max_recovery_attempts = 20;
    config.recovery_backoff_base_ms = 50;
    config.recovery_backoff_max_ms = 60000;

    config.Reset();

    EXPECT_FALSE(config.enable_auto_recovery);
    EXPECT_EQ(config.disconnect_threshold, 3u);
    EXPECT_EQ(config.max_recovery_attempts, 10u);
    EXPECT_EQ(config.recovery_backoff_base_ms, 1000u);
    EXPECT_EQ(config.recovery_backoff_max_ms, 30000u);
}

/**
 * @brief 测试 StreamMetrics 新增字段默认值
 */
TEST(CameraSourceRecoveryMetricsTest, DefaultValues)
{
    StreamMetrics metrics;

    EXPECT_EQ(metrics.disconnection_count, 0u);
    EXPECT_EQ(metrics.recovery_attempt_count, 0u);
    EXPECT_EQ(metrics.current_state, 0u);
}

/**
 * @brief 测试 CameraSource 初始状态
 */
TEST(CameraSourceRecoveryStateTest, InitialState)
{
    CameraSource source;

    EXPECT_EQ(source.GetState(), SourceState::kIdle);
    EXPECT_EQ(source.GetDisconnectionCount(), 0u);
    EXPECT_EQ(source.GetRecoveryAttemptCount(), 0u);
    EXPECT_FALSE(source.IsRunning());
}

/**
 * @brief 测试 CameraSource 状态枚举值
 */
TEST(CameraSourceRecoveryStateTest, StateEnumValues)
{
    EXPECT_EQ(static_cast<uint32_t>(SourceState::kIdle), 0u);
    EXPECT_EQ(static_cast<uint32_t>(SourceState::kReady), 1u);
    EXPECT_EQ(static_cast<uint32_t>(SourceState::kStreaming), 2u);
    EXPECT_EQ(static_cast<uint32_t>(SourceState::kDisconnected), 3u);
    EXPECT_EQ(static_cast<uint32_t>(SourceState::kRetrying), 4u);
    EXPECT_EQ(static_cast<uint32_t>(SourceState::kPermanentFailure), 5u);
}

/**
 * @brief 测试 CameraSource FillMetrics 填充新增字段
 */
TEST(CameraSourceRecoveryMetricsTest, FillMetrics)
{
    CameraSource source;
    StreamMetrics metrics;

    source.FillMetrics(&metrics);

    EXPECT_EQ(metrics.disconnection_count, 0u);
    EXPECT_EQ(metrics.recovery_attempt_count, 0u);
    EXPECT_EQ(metrics.current_state, static_cast<uint32_t>(SourceState::kIdle));
}

/**
 * @brief 测试未初始化的 CameraSource 不能 Start
 */
TEST(CameraSourceRecoveryLifecycleTest, StartWithoutInitializeFails)
{
    CameraSource source;

    EXPECT_FALSE(source.Start());
    EXPECT_EQ(source.GetState(), SourceState::kIdle);
    EXPECT_FALSE(source.IsRunning());
}

/**
 * @brief 测试 Stop 在 Idle 状态下幂等
 */
TEST(CameraSourceRecoveryLifecycleTest, StopWhenIdleIsIdempotent)
{
    CameraSource source;

    EXPECT_NO_THROW(source.Stop());
    EXPECT_EQ(source.GetState(), SourceState::kIdle);
    EXPECT_FALSE(source.IsRunning());
}

/**
 * @brief 测试自动恢复默认关闭的配置不影响现有行为
 */
TEST(CameraSourceRecoveryConfigTest, BackwardCompatibility)
{
    CameraConfig config = CameraConfig::GetDefault();

    // 默认关闭自动恢复
    EXPECT_FALSE(config.enable_auto_recovery);

    // 现有字段不受影响
    EXPECT_EQ(config.width_, 1920u);
    EXPECT_EQ(config.height_, 1080u);
    EXPECT_EQ(config.format_, PixelFormat::kNV12);
    EXPECT_EQ(config.fps_, 30u);
    EXPECT_EQ(config.buffer_count_, 4u);
    EXPECT_TRUE(config.IsValid());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
