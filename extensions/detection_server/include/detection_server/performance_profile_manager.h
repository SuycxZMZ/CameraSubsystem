#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_PERFORMANCE_PROFILE_MANAGER_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_PERFORMANCE_PROFILE_MANAGER_H

#include "detection_server/detection_types.h"

#include <string>

namespace camera_subsystem::extensions::detection_server {

class IPerformanceProfileManager
{
  public:
    virtual ~IPerformanceProfileManager() = default;

    virtual PerformanceProfileSnapshot ReadSnapshot() const = 0;
    virtual bool ApplyProfile(PerformanceProfile profile,
                              PerformanceProfileSnapshot* snapshot) const = 0;
};

class PerformanceProfileManager : public IPerformanceProfileManager
{
  public:
    explicit PerformanceProfileManager(std::string sysfs_root = std::string());

    PerformanceProfileSnapshot ReadSnapshot() const override;
    bool ApplyProfile(PerformanceProfile profile,
                      PerformanceProfileSnapshot* snapshot) const override;

  private:
    std::string sysfs_root_;

    std::string ResolvePath(const char* absolute_path) const;
    bool ReadTextFile(const std::string& path, std::string* value) const;
    bool ReadUint64File(const std::string& path, uint64_t* value) const;
    bool WriteTextFile(const std::string& path, const std::string& value) const;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_PERFORMANCE_PROFILE_MANAGER_H
