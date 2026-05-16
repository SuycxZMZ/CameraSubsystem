/**
 * @file metrics.h
 * @brief 统一 Metrics 接口定义
 * @author CameraSubsystem Team
 * @date 2026-05-10
 *
 * 按 stream_id 标签聚合的单路流指标快照，以及 IMetricsProvider 填充接口。
 *
 * @see docs/METRICS_INTERFACE_DESIGN.md
 */

#ifndef CAMERA_SUBSYSTEM_CORE_METRICS_H
#define CAMERA_SUBSYSTEM_CORE_METRICS_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace camera_subsystem
{
namespace core
{

/**
 * @brief 单路流指标快照
 *
 * 各模块通过 IMetricsProvider::FillMetrics() 填充自己负责的字段，
 * 未负责字段保持默认值 0。
 */
struct StreamMetrics
{
    std::string stream_id;
    uint64_t timestamp_ns = 0;

    // ---- 采集层 (CameraSource 负责) ----
    uint64_t capture_frame_count = 0;   // 总采集帧数
    uint64_t capture_dropped_count = 0; // 采集丢帧数
    uint64_t dma_buf_frame_count = 0;   // DMA-BUF 路径帧数
    uint64_t lease_exhausted_count = 0; // lease 耗尽次数
    size_t active_lease_count = 0;      // 当前活跃 lease 数

    // ---- 分发层 (FrameBroker 负责) ----
    uint64_t broker_published_count = 0;  // FrameBroker 收到发布的帧数
    uint64_t broker_dispatched_count = 0; // 成功分发给 subscriber 的帧数
    uint64_t broker_dropped_count = 0;    // FrameBroker 背压丢弃的帧数
    size_t broker_queue_depth = 0;        // 当前任务队列深度
    size_t broker_subscriber_count = 0;   // 当前活跃 subscriber 数

    // ---- 数据面 (DataPlaneV2 / publisher 负责) ----
    uint64_t v2_sent_frame_count = 0;     // DataPlaneV2 发送帧数
    uint64_t v2_send_failure_count = 0;   // DataPlaneV2 发送失败数
    uint64_t release_pending_count = 0;   // 待 release 帧数
    uint64_t release_timeout_count = 0;   // release 超时次数
    uint64_t release_reclaimed_count = 0; // 成功回收帧数

    // ---- 断连与恢复指标 (CameraSource 负责) ----
    uint64_t disconnection_count = 0;    // 断连发生次数
    uint64_t recovery_attempt_count = 0; // 恢复尝试总次数
    uint32_t current_state = 0;          // 当前 SourceState 值

    // ---- 降级指标 (CameraSource 负责) ----
    bool source_degraded = false;           // 是否处于降级模式
    uint32_t source_requested_fps = 0;      // 原始请求帧率
    uint32_t source_current_target_fps = 0; // 当前目标帧率
    uint64_t source_degradation_count = 0;       // 降级触发次数
    uint64_t source_degradation_recovery_count = 0; // 降级恢复次数
    uint64_t source_degradation_failure_count = 0;  // 降级/恢复失败次数
};

/**
 * @brief Metrics 提供方接口
 *
 * 实现方只填充自己负责的 StreamMetrics 字段，不修改其他字段。
 */
class IMetricsProvider
{
  public:
    virtual ~IMetricsProvider() = default;

    /**
     * @brief 填充指标快照
     * @param metrics 输出参数，provider 只填充自己负责的字段
     *
     * @note 实现方应保证线程安全（通常读取内部原子计数器）
     * @note metrics->stream_id 在调用前已由聚合方设置，provider 不应修改
     */
    virtual void FillMetrics(StreamMetrics* metrics) const = 0;
};

/**
 * @brief Metrics 聚合器
 *
 * 按 stream_id 注册多个 IMetricsProvider，合并生成单路流或全量流指标快照。
 */
class MetricsAggregator
{
  public:
    MetricsAggregator() = default;
    ~MetricsAggregator() = default;

    // 禁止拷贝
    MetricsAggregator(const MetricsAggregator&) = delete;
    MetricsAggregator& operator=(const MetricsAggregator&) = delete;

    /**
     * @brief 注册 provider 到指定 stream
     */
    void RegisterProvider(const std::string& stream_id, IMetricsProvider* provider);

    /**
     * @brief 注销 provider
     */
    void UnregisterProvider(const std::string& stream_id, IMetricsProvider* provider);

    /**
     * @brief 获取单路流指标快照
     */
    StreamMetrics GetStreamMetrics(const std::string& stream_id) const;

    /**
     * @brief 获取所有流指标快照
     */
    std::vector<StreamMetrics> GetAllStreamMetrics() const;

    /**
     * @brief 格式化单路流指标为日志字符串
     *
     * 输出格式示例：
     *   stream=usb0 | capture=1848 | dropped=0 | dmabuf=1848 | leases=0 | lease_exhausted=0
     *   | queue=0 | subscribers=2 | v2_sent=3693 | v2_fail=0 | release_pending=0 |
     * release_timeout=0
     */
    static std::string FormatForLog(const StreamMetrics& metrics);

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<IMetricsProvider*>> providers_by_stream_;
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_METRICS_H
