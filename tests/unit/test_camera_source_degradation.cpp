/**
 * @file test_camera_source_degradation.cpp
 * @brief CameraSource 降级策略第一阶段单元测试
 * @author CameraSubsystem Team
 * @date 2026-05-12
 *
 * 第一阶段范围：Config/Metrics/getter/单测，不接入动态 VIDIOC_S_PARM 主循环。
 */

#define private public
#include "camera_subsystem/camera/camera_source.h"
#undef private
#include "camera_subsystem/core/camera_config.h"
#include "camera_subsystem/core/metrics.h"
#include <gtest/gtest.h>

using namespace camera_subsystem;
using namespace camera_subsystem::camera;
using namespace camera_subsystem::core;

/**
 * @brief CameraConfig 降级字段默认值
 */
TEST(CameraSourceDegradationConfigTest, DefaultValues)
{
    CameraConfig config;

    EXPECT_EQ(config.enable_degradation, 0u);
    EXPECT_EQ(config.degradation_target_fps, 15u);
    EXPECT_EQ(config.degradation_window_sec, 5u);
    EXPECT_EQ(config.degradation_recovery_frames, 30u);
}

/**
 * @brief CameraConfig 结构大小保持 ABI 兼容
 */
TEST(CameraSourceDegradationConfigTest, StructSizeUnchanged)
{
    EXPECT_EQ(sizeof(CameraConfig), 88u);
}

/**
 * @brief CameraConfig 降级字段可修改
 */
TEST(CameraSourceDegradationConfigTest, ModifyDegradationFields)
{
    CameraConfig config;
    config.enable_degradation = 1;
    config.degradation_target_fps = 10;
    config.degradation_window_sec = 3;
    config.degradation_recovery_frames = 20;

    EXPECT_EQ(config.enable_degradation, 1u);
    EXPECT_EQ(config.degradation_target_fps, 10u);
    EXPECT_EQ(config.degradation_window_sec, 3u);
    EXPECT_EQ(config.degradation_recovery_frames, 20u);
}

/**
 * @brief CameraConfig Reset 恢复降级字段默认值
 */
TEST(CameraSourceDegradationConfigTest, ResetRestoresDefaults)
{
    CameraConfig config;
    config.enable_degradation = 1;
    config.degradation_target_fps = 10;
    config.degradation_window_sec = 3;
    config.degradation_recovery_frames = 20;

    config.Reset();

    EXPECT_EQ(config.enable_degradation, 0u);
    EXPECT_EQ(config.degradation_target_fps, 15u);
    EXPECT_EQ(config.degradation_window_sec, 5u);
    EXPECT_EQ(config.degradation_recovery_frames, 30u);
}

/**
 * @brief StreamMetrics 降级字段默认值
 */
TEST(CameraSourceDegradationMetricsTest, DefaultValues)
{
    StreamMetrics metrics;

    EXPECT_FALSE(metrics.source_degraded);
    EXPECT_EQ(metrics.source_requested_fps, 0u);
    EXPECT_EQ(metrics.source_current_target_fps, 0u);
    EXPECT_EQ(metrics.source_degradation_count, 0u);
    EXPECT_EQ(metrics.source_degradation_recovery_count, 0u);
    EXPECT_EQ(metrics.source_degradation_failure_count, 0u);
}

/**
 * @brief CameraSource 初始降级状态
 */
TEST(CameraSourceDegradationStateTest, InitialState)
{
    CameraSource source;

    EXPECT_FALSE(source.IsDegraded());
    EXPECT_EQ(source.GetDegradationCount(), 0u);
    EXPECT_EQ(source.GetDegradationRecoveryCount(), 0u);
    EXPECT_EQ(source.GetDegradationFailureCount(), 0u);
    EXPECT_EQ(source.GetRequestedFps(), 0u);
    EXPECT_EQ(source.GetCurrentTargetFps(), 0u);
}

/**
 * @brief CameraSource FillMetrics 填充降级字段
 */
TEST(CameraSourceDegradationMetricsTest, FillMetrics)
{
    CameraSource source;
    StreamMetrics metrics;

    source.FillMetrics(&metrics);

    EXPECT_FALSE(metrics.source_degraded);
    EXPECT_EQ(metrics.source_requested_fps, 0u);
    EXPECT_EQ(metrics.source_current_target_fps, 0u);
    EXPECT_EQ(metrics.source_degradation_count, 0u);
    EXPECT_EQ(metrics.source_degradation_recovery_count, 0u);
    EXPECT_EQ(metrics.source_degradation_failure_count, 0u);
}

/**
 * @brief CameraConfig GetDefault 包含正确的降级默认值
 */
TEST(CameraSourceDegradationConfigTest, GetDefaultValues)
{
    CameraConfig config = CameraConfig::GetDefault();

    EXPECT_EQ(config.fps_, 30u);
    EXPECT_EQ(config.enable_degradation, 0u);
    EXPECT_EQ(config.degradation_target_fps, 15u);
    EXPECT_EQ(config.degradation_window_sec, 5u);
    EXPECT_EQ(config.degradation_recovery_frames, 30u);
    EXPECT_TRUE(config.IsValid());
}

/**
 * @brief CameraConfig IsValid 拒绝非法降级配置
 */
TEST(CameraSourceDegradationConfigTest, IsValidRejectsInvalidDegradation)
{
    CameraConfig config = CameraConfig::GetDefault();
    config.enable_degradation = 1;

    // target_fps = 0 是无效的
    config.degradation_target_fps = 0;
    EXPECT_FALSE(config.IsValid());

    // window_sec = 0 是无效的
    config.degradation_target_fps = 15;
    config.degradation_window_sec = 0;
    EXPECT_FALSE(config.IsValid());

    // recovery_frames = 0 是无效的
    config.degradation_window_sec = 5;
    config.degradation_recovery_frames = 0;
    EXPECT_FALSE(config.IsValid());

    // 全部正确后恢复有效
    config.degradation_recovery_frames = 30;
    EXPECT_TRUE(config.IsValid());
}

/**
 * @brief CameraSource 默认配置不包含降级（向后兼容）
 */
TEST(CameraSourceDegradationStateTest, BackwardCompatibility)
{
    CameraSource source;

    EXPECT_FALSE(source.IsDegraded());
    EXPECT_EQ(source.GetRequestedFps(), 0u);
    EXPECT_EQ(source.GetCurrentTargetFps(), 0u);
    EXPECT_EQ(source.GetDegradationCount(), 0u);
}

/**
 * @brief FillMetrics 降级字段语义
 */
TEST(CameraSourceDegradationMetricsTest, FillMetricsSemantics)
{
    CameraSource source;
    StreamMetrics metrics;

    source.FillMetrics(&metrics);

    // 默认状态：未初始化，requested_fps = config_.fps_ = 0
    EXPECT_EQ(metrics.source_requested_fps, 0u);
    EXPECT_EQ(metrics.source_current_target_fps, 0u);
    EXPECT_FALSE(metrics.source_degraded);
}

/**
 * @brief UpdateDegradationState enable=0 时零侵入，不改变任何窗口状态
 */
TEST(CameraSourceDegradationStateTest, ZeroIntrusionWhenDisabled)
{
    CameraSource source;
    source.config_.enable_degradation = 0;
    source.config_.fps_ = 30;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;

    source.UpdateDegradationStateAt(1000000000ULL);
    EXPECT_EQ(source.window_start_ns_, 0u);
    EXPECT_EQ(source.window_frame_count_, 0u);
}

/**
 * @brief 滑动窗口：正常帧率不触发降级
 */
TEST(CameraSourceDegradationStateTest, NoDegradeWhenFpsNormal)
{
    CameraSource source;
    source.config_.enable_degradation = 1;
    source.config_.fps_ = 30;
    source.config_.degradation_window_sec = 1;
    source.config_.degradation_target_fps = 15;
    source.config_.degradation_recovery_frames = 5;
    source.is_degraded_ = false;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;
    source.degradation_failure_count_ = 0;

    const uint64_t base = 1000000000ULL;
    // 31 帧跨约 1.03 秒，窗口满，fps=30 >= 18，不降级
    for (uint32_t i = 0; i < 31; ++i)
    {
        source.UpdateDegradationStateAt(base + i * 33333333ULL);
    }
    EXPECT_EQ(source.degradation_failure_count_, 0u);
    EXPECT_FALSE(source.is_degraded_);
}

/**
 * @brief 滑动窗口：低帧率触发 ApplyDegradation（未初始化设备时失败）
 */
TEST(CameraSourceDegradationStateTest, DegradeTriggerWhenFpsLow)
{
    CameraSource source;
    source.config_.enable_degradation = 1;
    source.config_.fps_ = 30;
    source.config_.degradation_window_sec = 1;
    source.config_.degradation_target_fps = 15;
    source.config_.degradation_recovery_frames = 5;
    source.is_degraded_ = false;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;
    source.degradation_failure_count_ = 0;

    const uint64_t base = 1000000000ULL;
    // 1.1 秒内 11 帧，fps=10 < 18，触发 ApplyDegradation
    // device_fd_ < 0 导致 SetRequestedFps 失败，degradation_failure_count_ +1
    for (uint32_t i = 0; i < 11; ++i)
    {
        source.UpdateDegradationStateAt(base + i * 110000000ULL);
    }
    EXPECT_EQ(source.degradation_failure_count_, 1u);
    EXPECT_FALSE(source.is_degraded_);
}

/**
 * @brief 降级后：稳定帧率触发 RestoreOriginalFps（未初始化设备时失败）
 */
TEST(CameraSourceDegradationStateTest, RecoverTriggerWhenFpsStable)
{
    CameraSource source;
    source.config_.enable_degradation = 1;
    source.config_.fps_ = 30;
    source.config_.degradation_window_sec = 1;
    source.config_.degradation_target_fps = 15;
    source.config_.degradation_recovery_frames = 30;
    source.is_degraded_ = true;
    source.current_target_fps_ = 15;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;
    source.degradation_failure_count_ = 0;

    const uint64_t base = 1000000000ULL;
    // 30 帧跨约 2.0 秒，observed_fps=15 >= 12 (15*0.8)，触发一次 RestoreOriginalFps
    // device_fd_ < 0 导致失败，degradation_failure_count_ +1
    for (uint32_t i = 0; i < 30; ++i)
    {
        source.UpdateDegradationStateAt(base + i * 66666666ULL);
    }
    EXPECT_EQ(source.degradation_failure_count_, 1u);
    EXPECT_TRUE(source.is_degraded_);
}

/**
 * @brief 降级后：帧率仍低不触发恢复
 */
TEST(CameraSourceDegradationStateTest, NoRecoverWhenFpsStillLow)
{
    CameraSource source;
    source.config_.enable_degradation = 1;
    source.config_.fps_ = 30;
    source.config_.degradation_window_sec = 1;
    source.config_.degradation_target_fps = 15;
    source.config_.degradation_recovery_frames = 5;
    source.is_degraded_ = true;
    source.current_target_fps_ = 15;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;
    source.degradation_failure_count_ = 0;

    const uint64_t base = 1000000000ULL;
    // 5 帧在 550ms 内，fps=9.09 < 12 (15*0.8)，不恢复
    for (uint32_t i = 0; i < 5; ++i)
    {
        source.UpdateDegradationStateAt(base + i * 110000000ULL);
    }
    EXPECT_EQ(source.degradation_failure_count_, 0u);
    EXPECT_TRUE(source.is_degraded_);
}

/**
 * @brief trial restore 后若帧率仍低，会再次降级
 */
TEST(CameraSourceDegradationStateTest, TrialRestoreThenDegradeAgain)
{
    CameraSource source;
    source.config_.enable_degradation = 1;
    source.config_.fps_ = 30;
    source.config_.degradation_window_sec = 1;
    source.config_.degradation_target_fps = 15;
    source.config_.degradation_recovery_frames = 5;
    source.is_degraded_ = false;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;
    source.degradation_failure_count_ = 0;

    const uint64_t base = 1000000000ULL;

    // 阶段 1：低帧率触发降级（device_fd < 0，ApplyDegradation 失败）
    for (uint32_t i = 0; i < 11; ++i)
    {
        source.UpdateDegradationStateAt(base + i * 110000000ULL);
    }
    EXPECT_EQ(source.degradation_failure_count_, 1u);
    EXPECT_FALSE(source.is_degraded_);

    // 阶段 2：手动模拟 ApplyDegradation 成功（降级到 15fps）
    source.is_degraded_ = true;
    source.current_target_fps_ = 15;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;
    // 注意：degradation_failure_count_ 保持 1

    // 阶段 3：15fps 稳定，触发 trial restore（RestoreOriginalFps 失败）
    for (uint32_t i = 0; i < 5; ++i)
    {
        source.UpdateDegradationStateAt(base + 2000000000ULL + i * 66666666ULL);
    }
    EXPECT_EQ(source.degradation_failure_count_, 2u);
    EXPECT_TRUE(source.is_degraded_);

    // 阶段 4：手动模拟 RestoreOriginalFps 成功（恢复到 30fps）
    source.is_degraded_ = false;
    source.current_target_fps_ = 30;
    source.window_start_ns_ = 0;
    source.window_frame_count_ = 0;

    // 阶段 5：恢复后帧率仍低，再次降级
    for (uint32_t i = 0; i < 11; ++i)
    {
        source.UpdateDegradationStateAt(base + 3000000000ULL + i * 110000000ULL);
    }
    EXPECT_EQ(source.degradation_failure_count_, 3u);
    EXPECT_FALSE(source.is_degraded_);
}

/**
 * @brief Stop 后窗口状态被清理
 */
TEST(CameraSourceDegradationStateTest, StopClearsWindowState)
{
    CameraSource source;
    source.config_.enable_degradation = 1;
    source.config_.fps_ = 30;
    source.window_start_ns_ = 12345;
    source.window_frame_count_ = 10;
    // 让 Stop() 走过清理分支：is_running_ 或 state_ 不为 idle
    source.is_running_ = true;
    source.state_ = SourceState::kStreaming;

    source.Stop();

    EXPECT_EQ(source.window_start_ns_, 0u);
    EXPECT_EQ(source.window_frame_count_, 0u);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
