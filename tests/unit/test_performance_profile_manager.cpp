#include "detection_server/performance_profile_manager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace camera_subsystem::extensions::detection_server;

namespace {

class TempSysfsDir
{
  public:
    TempSysfsDir()
    {
        root_ = std::filesystem::temp_directory_path() /
                std::filesystem::path("camera_detection_sysfs_test_" +
                                      std::to_string(::getpid()) + "_" +
                                      std::to_string(counter_++));
        std::filesystem::create_directories(root_);
    }

    ~TempSysfsDir()
    {
        std::error_code error_code;
        std::filesystem::remove_all(root_, error_code);
    }

    const std::filesystem::path& root() const
    {
        return root_;
    }

    void WriteFile(const std::string& relative_path, const std::string& value) const
    {
        const auto full_path = root_ / relative_path;
        std::filesystem::create_directories(full_path.parent_path());
        std::ofstream output(full_path);
        output << value;
    }

  private:
    std::filesystem::path root_;
    inline static uint32_t counter_ = 0;
};

} // namespace

TEST(PerformanceProfileManagerTest, ApplyNpuCpuProfileWritesPerformanceGovernors)
{
    TempSysfsDir sysfs;
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/governor", "rknpu_ondemand\n");
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/cur_freq", "950000000\n");
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/max_freq", "950000000\n");
    sysfs.WriteFile("sys/devices/system/cpu/cpufreq/policy0/scaling_governor", "ondemand\n");
    sysfs.WriteFile("sys/devices/system/cpu/cpufreq/policy4/scaling_governor", "ondemand\n");

    PerformanceProfileManager manager(sysfs.root().string());
    PerformanceProfileSnapshot snapshot;

    ASSERT_TRUE(manager.ApplyProfile(PerformanceProfile::kNpuCpu, &snapshot));
    EXPECT_TRUE(snapshot.applied);
    EXPECT_EQ(snapshot.npu_governor, "performance");
    EXPECT_EQ(snapshot.cpu_policy0_governor, "performance");
    EXPECT_EQ(snapshot.cpu_policy4_governor, "performance");
    EXPECT_EQ(snapshot.npu_cur_freq_hz, 950000000U);
    EXPECT_EQ(snapshot.npu_max_freq_hz, 950000000U);
}

TEST(PerformanceProfileManagerTest, ApplyNoneProfileDoesNotRequireWritableNodes)
{
    TempSysfsDir sysfs;
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/governor", "rknpu_ondemand\n");
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/cur_freq", "300000000\n");
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/max_freq", "950000000\n");
    sysfs.WriteFile("sys/devices/system/cpu/cpufreq/policy0/scaling_governor", "ondemand\n");
    sysfs.WriteFile("sys/devices/system/cpu/cpufreq/policy4/scaling_governor", "ondemand\n");

    PerformanceProfileManager manager(sysfs.root().string());
    PerformanceProfileSnapshot snapshot;

    ASSERT_TRUE(manager.ApplyProfile(PerformanceProfile::kNone, &snapshot));
    EXPECT_TRUE(snapshot.applied);
    EXPECT_EQ(snapshot.npu_governor, "rknpu_ondemand");
    EXPECT_EQ(snapshot.cpu_policy0_governor, "ondemand");
}

TEST(PerformanceProfileManagerTest, MissingNodeFailsVerification)
{
    TempSysfsDir sysfs;
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/governor", "rknpu_ondemand\n");
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/cur_freq", "950000000\n");
    sysfs.WriteFile("sys/class/devfreq/27700000.npu/max_freq", "950000000\n");
    sysfs.WriteFile("sys/devices/system/cpu/cpufreq/policy0/scaling_governor", "ondemand\n");

    PerformanceProfileManager manager(sysfs.root().string());
    PerformanceProfileSnapshot snapshot;

    ASSERT_FALSE(manager.ApplyProfile(PerformanceProfile::kNpuCpu, &snapshot));
    EXPECT_FALSE(snapshot.applied);
    EXPECT_EQ(snapshot.error_code, DetectionErrorCode::kPerformanceProfileFailed);
}

TEST(PerformanceProfileManagerTest, FullProfileIsRejectedInPhaseOne)
{
    TempSysfsDir sysfs;
    PerformanceProfileManager manager(sysfs.root().string());
    PerformanceProfileSnapshot snapshot;

    ASSERT_FALSE(manager.ApplyProfile(PerformanceProfile::kFull, &snapshot));
    EXPECT_EQ(snapshot.error_code, DetectionErrorCode::kInvalidConfig);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
