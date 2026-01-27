/**
 * @file platform_logger.h
 * @brief 平台日志系统封装
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_LOGGER_H
#define CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_LOGGER_H

#include "../core/types.h"
#include <cstdarg>
#include <string>

namespace camera_subsystem {
namespace platform {

using core::LogLevel;

/**
 * @brief 平台日志系统类
 *
 * 封装日志操作,提供统一的日志接口。
 * 使用 spdlog 作为底层实现,支持按模块分级。
 */
class PlatformLogger
{
public:
    /**
     * @brief 初始化日志系统
     * @param log_file 日志文件路径,空字符串表示仅输出到控制台
     * @param level 日志级别
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数是线程安全的
     * @note 必须在任何日志操作之前调用
     */
    static bool Initialize(
        const std::string& log_file,
        LogLevel level = LogLevel::kInfo
    );

    /**
     * @brief 记录日志
     * @param level 日志级别
     * @param module 模块名称
     * @param format 格式化字符串
     * @param ... 可变参数
     *
     * @note 该函数是线程安全的
     * @note 热路径 (OnFrame 回调内) 建议仅打印 ERROR 日志
     */
    static void Log(
        LogLevel level,
        const char* module,
        const char* format,
        ...
    );

    /**
     * @brief 设置日志级别
     * @param level 日志级别
     *
     * @note 该函数是线程安全的
     */
    static void SetLogLevel(LogLevel level);

    /**
     * @brief 获取当前日志级别
     * @return 当前日志级别
     *
     * @note 该函数是线程安全的
     */
    static LogLevel GetLogLevel();

    /**
     * @brief 关闭日志系统
     *
     * @note 该函数是线程安全的
     * @note 关闭后不能再记录日志
     */
    static void Shutdown();

    /**
     * @brief 检查日志系统是否已初始化
     * @return true 表示已初始化,false 表示未初始化
     *
     * @note 该函数是线程安全的
     */
    static bool IsInitialized();

private:
    static bool s_initialized_;
    static LogLevel s_level_;
};

// 日志宏定义
#define LOG_TRACE(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kTrace, module, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kDebug, module, fmt, ##__VA_ARGS__)

#define LOG_INFO(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kInfo, module, fmt, ##__VA_ARGS__)

#define LOG_WARN(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kWarning, module, fmt, ##__VA_ARGS__)

#define LOG_ERROR(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kError, module, fmt, ##__VA_ARGS__)

#define LOG_CRITICAL(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kCritical, module, fmt, ##__VA_ARGS__)

} // namespace platform
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_LOGGER_H
