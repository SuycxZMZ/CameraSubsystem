#include "camera_subsystem/platform/platform_logger.h"

#include <cstdio>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>

namespace camera_subsystem {
namespace platform {

namespace {

spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
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

} // namespace

bool PlatformLogger::s_initialized_ = false;
LogLevel PlatformLogger::s_level_ = LogLevel::kInfo;

bool PlatformLogger::Initialize(const std::string& /*log_file*/, LogLevel level)
{
    static std::mutex init_mutex;
    const std::lock_guard<std::mutex> lock(init_mutex);

    s_level_ = level;
    spdlog::set_level(ToSpdlogLevel(level));
    s_initialized_ = true;
    return true;
}

void PlatformLogger::Log(LogLevel level, const char* module, const char* format, ...)
{
    if (!s_initialized_ || level < s_level_ || format == nullptr)
    {
        return;
    }

    va_list args;
    va_start(args, format);

    // 先测量需要的缓冲区大小
    va_list args_copy;
    va_copy(args_copy, args);
    const int required = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (required <= 0)
    {
        va_end(args);
        return;
    }

    std::vector<char> buffer(static_cast<std::size_t>(required) + 1U, '\0');
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    const char* safe_module = (module != nullptr) ? module : "unknown";

    spdlog::log(
        ToSpdlogLevel(level),
        "[%s] %s",
        safe_module,
        buffer.data()
    );
}

void PlatformLogger::SetLogLevel(LogLevel level)
{
    s_level_ = level;
    spdlog::set_level(ToSpdlogLevel(level));
}

LogLevel PlatformLogger::GetLogLevel()
{
    return s_level_;
}

void PlatformLogger::Shutdown()
{
    s_initialized_ = false;
    spdlog::shutdown();
}

bool PlatformLogger::IsInitialized()
{
    return s_initialized_;
}

} // namespace platform
} // namespace camera_subsystem

