/**
 * @file camera_config.h
 * @brief Camera 配置结构定义
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_CORE_CAMERA_CONFIG_H
#define CAMERA_SUBSYSTEM_CORE_CAMERA_CONFIG_H

#include "types.h"
#include <cstdint>

namespace camera_subsystem
{
namespace core
{

/**
 * @brief Camera 配置结构
 */
struct CameraConfig
{
    uint32_t width_;        // 图像宽度 (像素)
    uint32_t height_;       // 图像高度 (像素)
    PixelFormat format_;    // 像素格式
    uint32_t fps_;          // 帧率
    uint32_t buffer_count_; // Buffer 数量
    uint32_t io_method_;    // IO 方法 (IoMethod)
    uint8_t reserved_[64];  // 预留扩展空间

    /**
     * @brief 默认构造函数
     */
    CameraConfig();

    /**
     * @brief 构造函数
     * @param width 图像宽度
     * @param height 图像高度
     * @param format 像素格式
     * @param fps 帧率
     * @param buffer_count Buffer 数量
     * @param io_method IO 方法
     */
    CameraConfig(uint32_t width, uint32_t height, PixelFormat format, uint32_t fps,
                 uint32_t buffer_count,
                 uint32_t io_method = static_cast<uint32_t>(IoMethod::kDmaBuf));

    /**
     * @brief 检查配置是否有效
     * @return true 表示有效,false 表示无效
     */
    bool IsValid() const;

    /**
     * @brief 重置配置为默认值
     */
    void Reset();

    /**
     * @brief 获取默认配置 (1080p@30fps, NV12)
     * @return 默认配置
     */
    static CameraConfig GetDefault();
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_CAMERA_CONFIG_H
