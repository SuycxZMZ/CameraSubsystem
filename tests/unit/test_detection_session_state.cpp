#include "detection_server/detection_session.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

using namespace camera_subsystem::extensions::detection_server;

namespace {

class FakeProfileManager final : public IPerformanceProfileManager
{
  public:
    explicit FakeProfileManager(bool should_succeed) : should_succeed_(should_succeed) {}

    PerformanceProfileSnapshot ReadSnapshot() const override
    {
        return snapshot_;
    }

    bool ApplyProfile(PerformanceProfile profile, PerformanceProfileSnapshot* snapshot) const override
    {
        PerformanceProfileSnapshot local_snapshot;
        local_snapshot.profile = profile;
        local_snapshot.applied = should_succeed_;
        local_snapshot.npu_governor = should_succeed_ ? "performance" : "rknpu_ondemand";
        local_snapshot.cpu_policy0_governor = should_succeed_ ? "performance" : "ondemand";
        local_snapshot.cpu_policy4_governor = should_succeed_ ? "performance" : "ondemand";
        local_snapshot.error_code = should_succeed_ ? DetectionErrorCode::kOk
                                                    : DetectionErrorCode::kPerformanceProfileFailed;
        local_snapshot.message = should_succeed_ ? std::string() : "profile failed";
        if (snapshot)
        {
            *snapshot = local_snapshot;
        }
        snapshot_ = local_snapshot;
        return should_succeed_;
    }

  private:
    bool should_succeed_ = true;
    mutable PerformanceProfileSnapshot snapshot_;
};

class FakeModelSession final : public IRknnModelSession
{
  public:
    explicit FakeModelSession(bool should_succeed) : should_succeed_(should_succeed) {}

    bool Initialize(const std::string& model_path, uint32_t core_mask,
                    std::string* error_message) override
    {
        if (!should_succeed_)
        {
            initialized_ = false;
            last_error_code_ = DetectionErrorCode::kModelLoadFailed;
            last_error_message_ = "model init failed";
            if (error_message)
            {
                *error_message = last_error_message_;
            }
            return false;
        }

        initialized_ = true;
        runtime_info_.model_name = model_path;
        runtime_info_.npu_core_mask = core_mask;
        runtime_info_.runtime_version = "fake-runtime";
        runtime_info_.driver_version = "fake-driver";
        last_error_code_ = DetectionErrorCode::kOk;
        last_error_message_.clear();
        if (error_message)
        {
            error_message->clear();
        }
        return true;
    }

    void Shutdown() override
    {
        initialized_ = false;
        runtime_info_ = RknnRuntimeInfo();
    }

    bool IsInitialized() const override
    {
        return initialized_;
    }

    DetectionErrorCode GetLastErrorCode() const override
    {
        return last_error_code_;
    }

    std::string GetLastErrorMessage() const override
    {
        return last_error_message_;
    }

    RknnRuntimeInfo GetRuntimeInfo() const override
    {
        return runtime_info_;
    }

  private:
    bool should_succeed_ = true;
    bool initialized_ = false;
    DetectionErrorCode last_error_code_ = DetectionErrorCode::kOk;
    std::string last_error_message_;
    RknnRuntimeInfo runtime_info_;
};

} // namespace

TEST(DetectionSessionStateTest, StartTransitionsToRunning)
{
    DetectionServerConfig config;
    auto model_session = std::make_unique<FakeModelSession>(true);
    auto profile_manager = std::make_shared<FakeProfileManager>(true);

    DetectionSession session(std::move(config), std::move(model_session), profile_manager);
    ASSERT_TRUE(session.Start());

    const DetectionSessionSnapshot snapshot = session.GetSnapshot();
    EXPECT_EQ(snapshot.state, DetectionState::kRunning);
    EXPECT_EQ(snapshot.error_code, DetectionErrorCode::kOk);
    EXPECT_TRUE(snapshot.performance_profile.applied);
}

TEST(DetectionSessionStateTest, ProfileFailureBlocksStartByDefault)
{
    DetectionServerConfig config;
    auto model_session = std::make_unique<FakeModelSession>(true);
    auto profile_manager = std::make_shared<FakeProfileManager>(false);

    DetectionSession session(std::move(config), std::move(model_session), profile_manager);
    ASSERT_FALSE(session.Start());

    const DetectionSessionSnapshot snapshot = session.GetSnapshot();
    EXPECT_EQ(snapshot.state, DetectionState::kError);
    EXPECT_EQ(snapshot.error_code, DetectionErrorCode::kPerformanceProfileFailed);
}

TEST(DetectionSessionStateTest, AllowedProfileFailureStillStarts)
{
    DetectionServerConfig config;
    config.allow_performance_profile_failure = true;
    auto model_session = std::make_unique<FakeModelSession>(true);
    auto profile_manager = std::make_shared<FakeProfileManager>(false);

    DetectionSession session(std::move(config), std::move(model_session), profile_manager);
    ASSERT_TRUE(session.Start());

    const DetectionSessionSnapshot snapshot = session.GetSnapshot();
    EXPECT_EQ(snapshot.state, DetectionState::kRunning);
    EXPECT_EQ(snapshot.error_code, DetectionErrorCode::kPerformanceProfileFailed);
    EXPECT_FALSE(snapshot.performance_profile.applied);
}

TEST(DetectionSessionStateTest, ModelFailureTransitionsToError)
{
    DetectionServerConfig config;
    auto model_session = std::make_unique<FakeModelSession>(false);
    auto profile_manager = std::make_shared<FakeProfileManager>(true);

    DetectionSession session(std::move(config), std::move(model_session), profile_manager);
    ASSERT_FALSE(session.Start());

    const DetectionSessionSnapshot snapshot = session.GetSnapshot();
    EXPECT_EQ(snapshot.state, DetectionState::kError);
    EXPECT_EQ(snapshot.error_code, DetectionErrorCode::kModelLoadFailed);
}

TEST(DetectionSessionStateTest, StopReturnsToIdle)
{
    DetectionServerConfig config;
    auto model_session = std::make_unique<FakeModelSession>(true);
    auto profile_manager = std::make_shared<FakeProfileManager>(true);

    DetectionSession session(std::move(config), std::move(model_session), profile_manager);
    ASSERT_TRUE(session.Start());
    ASSERT_TRUE(session.Stop());

    const DetectionSessionSnapshot snapshot = session.GetSnapshot();
    EXPECT_EQ(snapshot.state, DetectionState::kIdle);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
