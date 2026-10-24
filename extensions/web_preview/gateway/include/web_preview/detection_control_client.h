#ifndef WEB_PREVIEW_DETECTION_CONTROL_CLIENT_H
#define WEB_PREVIEW_DETECTION_CONTROL_CLIENT_H

#include <cstdint>
#include <optional>
#include <mutex>
#include <string>

namespace web_preview {

/**
 * @brief 检测运行时配置
 */
struct DetectionConfig
{
    uint32_t infer_every_n_frames = 1;
    double score_threshold = 0.25;
    double nms_threshold = 0.45;
};

/**
 * @brief 检测性能指标
 */
struct DetectionMetrics
{
    uint64_t input_frames = 0;
    uint64_t inferred_frames = 0;
    uint32_t last_object_count = 0;
};

/**
 * @brief 检测状态查询结果
 */
struct DetectionStatusResult
{
    bool available = false;
    std::string error;
    std::string state;
    std::string model_name;
    uint32_t npu_core_mask = 0;
    bool performance_profile_applied = false;
    DetectionConfig config;
    DetectionMetrics metrics;
};

/**
 * @brief 检测控制命令结果
 */
struct DetectionControlResult
{
    bool ok = false;
    std::string error_code;
    std::string message;
    std::string state;
};

/**
 * @brief Detection Server 控制面客户端
 *
 * 通过 Unix Domain Socket 与 camera_detection_server 通信，
 * 发送 JSON line 命令并接收响应。
 */
class DetectionControlClient
{
public:
    explicit DetectionControlClient(const std::string& socket_path);
    ~DetectionControlClient() = default;

    // 禁止拷贝
    DetectionControlClient(const DetectionControlClient&) = delete;
    DetectionControlClient& operator=(const DetectionControlClient&) = delete;

    /**
     * @brief 查询检测服务状态
     * @return 检测状态结果，available=false 表示服务不可用
     */
    DetectionStatusResult GetDetectionStatus();

    /**
     * @brief 启动检测
     * @return 控制命令结果
     */
    DetectionControlResult StartDetection();

    /**
     * @brief 停止检测
     * @return 控制命令结果
     */
    DetectionControlResult StopDetection();

    /**
     * @brief 更新检测配置
     * @param infer_every_n_frames 推理间隔帧数
     * @param score_threshold 置信度阈值
     * @param nms_threshold NMS 阈值
     * @return 控制命令结果
     */
    DetectionControlResult SetDetectionConfig(std::optional<uint32_t> infer_every_n_frames,
                                              std::optional<double> score_threshold,
                                              std::optional<double> nms_threshold);

    int last_errno() const { return last_errno_; }

private:
    int Connect();
    std::string SendCommand(const std::string& json_line);
    std::string BuildRequestId();

    std::string socket_path_;
    std::mutex mutex_;
    uint64_t request_counter_ = 0;
    int last_errno_ = 0;
};

} // namespace web_preview

#endif // WEB_PREVIEW_DETECTION_CONTROL_CLIENT_H
