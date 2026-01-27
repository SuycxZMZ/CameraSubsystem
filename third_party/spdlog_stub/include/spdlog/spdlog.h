#ifndef SPDLOG_SPDLOG_H
#define SPDLOG_SPDLOG_H

#include <string>

namespace spdlog {

namespace level {
enum level_enum
{
    trace = 0,
    debug,
    info,
    warn,
    err,
    critical,
    off
};
} // namespace level

inline void set_level(level::level_enum) {}
inline void set_pattern(const std::string&) {}
inline void shutdown() {}

template <typename... Args>
inline void log(level::level_enum, const char*, Args&&...) {}

template <typename... Args>
inline void trace(const char*, Args&&...) {}

template <typename... Args>
inline void debug(const char*, Args&&...) {}

template <typename... Args>
inline void info(const char*, Args&&...) {}

template <typename... Args>
inline void warn(const char*, Args&&...) {}

template <typename... Args>
inline void error(const char*, Args&&...) {}

template <typename... Args>
inline void critical(const char*, Args&&...) {}

} // namespace spdlog

#endif // SPDLOG_SPDLOG_H

