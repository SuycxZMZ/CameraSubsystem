#include "detection_server/performance_profile_manager.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace camera_subsystem::extensions::detection_server {
namespace {

constexpr const char* kNpuGovernorPath = "/sys/class/devfreq/27700000.npu/governor";
constexpr const char* kNpuCurFreqPath = "/sys/class/devfreq/27700000.npu/cur_freq";
constexpr const char* kNpuMaxFreqPath = "/sys/class/devfreq/27700000.npu/max_freq";
constexpr const char* kCpuPolicy0GovernorPath = "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor";
constexpr const char* kCpuPolicy4GovernorPath = "/sys/devices/system/cpu/cpufreq/policy4/scaling_governor";

std::string TrimTrailingWhitespace(std::string value)
{
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
            value.back() == '\t'))
    {
        value.pop_back();
    }
    return value;
}

bool ParseUint64String(const std::string& value, uint64_t* parsed_value)
{
    if (!parsed_value)
    {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0')
    {
        return false;
    }

    *parsed_value = static_cast<uint64_t>(parsed);
    return true;
}

} // namespace

PerformanceProfileManager::PerformanceProfileManager(std::string sysfs_root)
    : sysfs_root_(std::move(sysfs_root))
{
}

PerformanceProfileSnapshot PerformanceProfileManager::ReadSnapshot() const
{
    PerformanceProfileSnapshot snapshot;

    ReadTextFile(ResolvePath(kNpuGovernorPath), &snapshot.npu_governor);
    ReadUint64File(ResolvePath(kNpuCurFreqPath), &snapshot.npu_cur_freq_hz);
    ReadUint64File(ResolvePath(kNpuMaxFreqPath), &snapshot.npu_max_freq_hz);
    ReadTextFile(ResolvePath(kCpuPolicy0GovernorPath), &snapshot.cpu_policy0_governor);
    ReadTextFile(ResolvePath(kCpuPolicy4GovernorPath), &snapshot.cpu_policy4_governor);

    return snapshot;
}

bool PerformanceProfileManager::ApplyProfile(PerformanceProfile profile,
                                             PerformanceProfileSnapshot* snapshot) const
{
    PerformanceProfileSnapshot local_snapshot = ReadSnapshot();
    local_snapshot.profile = profile;

    auto fail = [&](DetectionErrorCode error_code, const std::string& message) -> bool
    {
        local_snapshot.applied = false;
        local_snapshot.error_code = error_code;
        local_snapshot.message = message;
        if (snapshot)
        {
            *snapshot = local_snapshot;
        }
        return false;
    };

    if (profile == PerformanceProfile::kFull)
    {
        return fail(DetectionErrorCode::kInvalidConfig,
                    "performance_profile=full is not supported in phase 1");
    }

    if (profile == PerformanceProfile::kNone)
    {
        local_snapshot.applied = true;
        local_snapshot.error_code = DetectionErrorCode::kOk;
        if (snapshot)
        {
            *snapshot = local_snapshot;
        }
        return true;
    }

    if (!WriteTextFile(ResolvePath(kNpuGovernorPath), "performance"))
    {
        return fail(DetectionErrorCode::kPerformanceProfileFailed,
                    "failed to set NPU governor to performance");
    }

    if (profile == PerformanceProfile::kNpuCpu)
    {
        if (!WriteTextFile(ResolvePath(kCpuPolicy0GovernorPath), "performance"))
        {
            return fail(DetectionErrorCode::kPerformanceProfileFailed,
                        "failed to set CPU policy0 governor to performance");
        }
        if (!WriteTextFile(ResolvePath(kCpuPolicy4GovernorPath), "performance"))
        {
            return fail(DetectionErrorCode::kPerformanceProfileFailed,
                        "failed to set CPU policy4 governor to performance");
        }
    }

    local_snapshot = ReadSnapshot();
    local_snapshot.profile = profile;

    if (local_snapshot.npu_governor != "performance")
    {
        return fail(DetectionErrorCode::kPerformanceProfileFailed,
                    "NPU governor verification failed");
    }
    if (profile == PerformanceProfile::kNpuCpu &&
        (local_snapshot.cpu_policy0_governor != "performance" ||
         local_snapshot.cpu_policy4_governor != "performance"))
    {
        return fail(DetectionErrorCode::kPerformanceProfileFailed,
                    "CPU governor verification failed");
    }

    local_snapshot.applied = true;
    local_snapshot.error_code = DetectionErrorCode::kOk;
    local_snapshot.message.clear();
    if (snapshot)
    {
        *snapshot = local_snapshot;
    }
    return true;
}

std::string PerformanceProfileManager::ResolvePath(const char* absolute_path) const
{
    if (sysfs_root_.empty())
    {
        return absolute_path;
    }

    if (absolute_path[0] == '/')
    {
        return sysfs_root_ + absolute_path;
    }

    return sysfs_root_ + "/" + absolute_path;
}

bool PerformanceProfileManager::ReadTextFile(const std::string& path, std::string* value) const
{
    if (!value)
    {
        return false;
    }

    std::ifstream input(path);
    if (!input.is_open())
    {
        return false;
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    *value = TrimTrailingWhitespace(stream.str());
    return true;
}

bool PerformanceProfileManager::ReadUint64File(const std::string& path, uint64_t* value) const
{
    std::string raw_value;
    if (!ReadTextFile(path, &raw_value))
    {
        return false;
    }

    return ParseUint64String(raw_value, value);
}

bool PerformanceProfileManager::WriteTextFile(const std::string& path, const std::string& value) const
{
    std::ofstream output(path);
    if (!output.is_open())
    {
        return false;
    }

    output << value;
    output.flush();
    return output.good();
}

} // namespace camera_subsystem::extensions::detection_server
