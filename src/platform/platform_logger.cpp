/**
 * @file platform_logger.cpp
 * @brief 平台日志系统实现
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/platform/platform_logger.h"
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <vector>

// 尝试包含spdlog
#ifdef SPDLOG_COMPILED_LIB
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#define HAS_SPDLOG 1
#else
#define HAS_SPDLOG 0
#endif

namespace camera_subsystem
{
namespace platform
{

// 静态成员初始化
bool PlatformLogger::s_initialized_ = false;
LogLevel PlatformLogger::s_level_ = LogLevel::kInfo;
std::mutex PlatformLogger::s_mutex_;

#if HAS_SPDLOG
static std::shared_ptr<spdlog::logger> s_logger_;
#endif

#if HAS_SPDLOG
static spdlog::level::level_enum SpdlogLevelFromLogLevel(LogLevel level)
{
    switch (level)
    {
        case LogLevel::kTrace:
            return spdlog::level::trace;
        case LogLevel::kDebug:
            return spdlog::level::debug;
        case LogLevel::kInfo:
            return spdlog::level::info;
        case LogLevel::kWarning:
            return spdlog::level::warn;
        case LogLevel::kError:
            return spdlog::level::err;
        case LogLevel::kCritical:
            return spdlog::level::critical;
        default:
            return spdlog::level::info;
    }
}
#endif

bool PlatformLogger::Initialize(const std::string& log_file, LogLevel level)
{
    std::lock_guard<std::mutex> lock(s_mutex_);

    if (s_initialized_)
    {
        return true;
    }

    s_level_ = level;

#if HAS_SPDLOG
    try
    {
        // 创建异步日志
        spdlog::init_thread_pool(8192, 1);

        std::vector<spdlog::sink_ptr> sinks;

        // 控制台输出
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] [%n] %v");
        sinks.push_back(console_sink);

        // 文件输出（如果指定了文件路径）
        if (!log_file.empty())
        {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file, 1024 * 1024 * 100, 3); // 100MB per file, 3 files
            file_sink->set_level(spdlog::level::trace);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%l] [%n] %v");
            sinks.push_back(file_sink);
        }

        // 创建异步logger
        s_logger_ = std::make_shared<spdlog::async_logger>("camera_subsystem", sinks.begin(),
                                                           sinks.end(), spdlog::thread_pool(),
                                                           spdlog::async_overflow_policy::block);

        s_logger_->set_level(SpdlogLevelFromLogLevel(level));
        s_logger_->flush_on(spdlog::level::err);

        spdlog::register_logger(s_logger_);
        spdlog::set_default_logger(s_logger_);

        s_initialized_ = true;
        return true;
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        fprintf(stderr, "Failed to initialize spdlog: %s\n", ex.what());
        return false;
    }
#else
    // 如果没有spdlog，使用简单的控制台输出
    s_initialized_ = true;
    return true;
#endif
}

void PlatformLogger::Log(LogLevel level, const char* module, const char* format, ...)
{
    std::lock_guard<std::mutex> lock(s_mutex_);

    if (!s_initialized_ || level < s_level_)
    {
        return;
    }

#if HAS_SPDLOG
    if (s_logger_)
    {
        va_list args;
        va_start(args, format);

        // 格式化消息
        char buffer[4096];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // 记录日志
        s_logger_->log(SpdlogLevelFromLogLevel(level), "[{}] {}", module, buffer);
    }
#else
    // 简单的控制台输出
    va_list args;
    va_start(args, format);

    // 获取当前时间
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // 获取线程ID
    pthread_t tid = pthread_self();
    printf("[%s] [%lu] [%s] [%s] ", time_str, (unsigned long)tid, LevelToString(level), module);
    vprintf(format, args);
    printf("\n");

    va_end(args);
#endif
}

void PlatformLogger::SetLogLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(s_mutex_);
    s_level_ = level;

#if HAS_SPDLOG
    if (s_logger_)
    {
        s_logger_->set_level(SpdlogLevelFromLogLevel(level));
    }
#endif
}

LogLevel PlatformLogger::GetLogLevel()
{
    std::lock_guard<std::mutex> lock(s_mutex_);
    return s_level_;
}

void PlatformLogger::Shutdown()
{
    std::lock_guard<std::mutex> lock(s_mutex_);

    if (!s_initialized_)
    {
        return;
    }

#if HAS_SPDLOG
    if (s_logger_)
    {
        s_logger_->flush();
        spdlog::drop_all();
        s_logger_.reset();
    }
    spdlog::shutdown();
#endif

    s_initialized_ = false;
}

bool PlatformLogger::IsInitialized()
{
    std::lock_guard<std::mutex> lock(s_mutex_);
    return s_initialized_;
}

const char* PlatformLogger::LevelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarning:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kCritical:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

} // namespace platform
} // namespace camera_subsystem
