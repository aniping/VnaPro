#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include "VnaIntegrationTestSupport.h"

namespace {

vna::VnaSweepPlan repeatedPlan(const vna::VnaSweepPlan &baseline, std::uint32_t pointCount)
{
    // 复用已校验的单点结构，只改稳定身份和频率，构造不会瞬时结束的长扫描。
    vna::VnaSweepPlan result;
    result.trigger = baseline.trigger;
    result.policy = baseline.policy;
    for (std::uint32_t index = 0; index < pointCount; ++index) {
        auto point = baseline.points.front();
        point.pointId = index + 1;
        point.frequencyHz += static_cast<double>(index) * 1.0e6;
        result.points.push_back(std::move(point));
    }
    return result;
}

class VnaDriverIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 从受信目录加载真实 MODULE，并通过异步发现取得宿主 DeviceId。
        auto managerResult = vna::VnaHardwareManager::create(VNA_TEST_DRIVER_DIRECTORY);
        ASSERT_TRUE(managerResult.hasValue()) << managerResult.error().description;
        manager_ = managerResult.takeValue();
        vna::test::DiscoverySink discovery;
        manager_->discoverAsync(vna::DiscoveryOptions{}, discovery);
        ASSERT_TRUE(discovery.wait());
        ASSERT_TRUE(discovery.succeeded());

        // 订阅必须早于异步命令，否则没有订阅者时控制事件会被主动丢弃。
        auto hardwareResult = manager_->open(discovery.firstDevice().deviceId);
        ASSERT_TRUE(hardwareResult.hasValue()) << hardwareResult.error().description;
        hardware_ = hardwareResult.takeValue();
        subscription_ = hardware_->subscribe(events_);
        const vna::CommandId connectCommand = hardware_->connectAsync();
        ASSERT_TRUE(events_.waitConnected());
        EXPECT_TRUE(events_.completedCommand(connectCommand));
        EXPECT_TRUE(hardware_->capabilities().supportsInternalTrigger);
        EXPECT_FALSE(hardware_->capabilities().supportsExternalTrigger);
        EXPECT_FALSE(hardware_->capabilities().perPointDwell);

        // 重复准备产生新 ConfigurationId，旧代次必须被拒绝。
        plan_ = vna::test::makePlan();
        ASSERT_TRUE(vna::isValid(hardware_->validate(plan_)));
        const vna::CommandId firstPrepare = hardware_->prepareAsync(plan_);
        ASSERT_TRUE(events_.waitPrepared(1));
        EXPECT_TRUE(events_.completedCommand(firstPrepare));
        const vna::ConfigurationId expiredConfiguration = events_.configuration();
        const vna::CommandId secondPrepare = hardware_->prepareAsync(plan_);
        ASSERT_TRUE(events_.waitPrepared(2));
        EXPECT_TRUE(events_.completedCommand(secondPrepare));
        configuration_ = events_.configuration();
        const vna::CommandId expiredStart = hardware_->startAsync(expiredConfiguration);
        ASSERT_TRUE(events_.waitFailures(1));
        EXPECT_TRUE(events_.failedCommand(expiredStart));
    }

    void TearDown() override
    {
        // 断言提前失败时也必须收走后台线程和动态库，避免污染后续用例。
        if (manager_) {
            manager_->shutdown();
        }
    }

    std::unique_ptr<vna::VnaHardwareManager> manager_;
    std::shared_ptr<vna::IVnaHardware> hardware_;
    vna::test::HardwareSink events_;
    vna::Subscription subscription_;
    vna::VnaSweepPlan plan_;
    vna::ConfigurationId configuration_{0};
};

TEST_F(VnaDriverIntegrationTest, StreamsAndAbortsSimulatorSweeps)
{
    // 完整扫频验证批次、顺序号和确定性波值。
    const vna::CommandId completedStart = hardware_->startAsync(configuration_);
    ASSERT_TRUE(events_.waitCompleted(1));
    EXPECT_TRUE(events_.completedCommand(completedStart));
    vna::test::verifyCompletedSweep(events_);
    events_.clearBlocks();

    // 在 StartCompleted 回调内立即中止，覆盖 Worker 尚未进入 Execute 的交接竞态。
    events_.abortOnNextStart(*hardware_);
    const vna::CommandId abortedStart = hardware_->startAsync(configuration_);
    ASSERT_TRUE(events_.waitStarted(2));
    ASSERT_TRUE(events_.waitCompleted(2));
    ASSERT_TRUE(events_.waitAborted(1));
    const vna::CommandId abortCommand = events_.triggeredAbortCommand();
    ASSERT_NE(abortCommand, 0U);
    EXPECT_EQ(events_.completion(1).reason, vna::SweepTerminationReason::Aborted);
    EXPECT_TRUE(events_.abortFollowedSweep());
    EXPECT_TRUE(events_.completedCommand(abortedStart));
    EXPECT_TRUE(events_.completedCommand(abortCommand));
    EXPECT_EQ(events_.failureCount(), 1U);
    events_.clearBlocks();

    // 中止标志只属于一次 SweepId，同一配置随后仍可完整执行。
    const vna::CommandId repeatedStart = hardware_->startAsync(configuration_);
    ASSERT_TRUE(events_.waitCompleted(3));
    EXPECT_EQ(events_.completion(2).reason, vna::SweepTerminationReason::Completed);
    EXPECT_TRUE(events_.completedCommand(repeatedStart));

    // 在所有波块释放后断开，验证完整生命周期收尾；manager 由 TearDown 关闭。
    const vna::CommandId disconnectCommand = hardware_->disconnectAsync();
    ASSERT_TRUE(events_.waitState(vna::DeviceState::Disconnected));
    ASSERT_TRUE(events_.waitDisconnected(1));
    EXPECT_TRUE(events_.disconnectFollowedState());
    EXPECT_TRUE(events_.completedCommand(disconnectCommand));
}

TEST_F(VnaDriverIntegrationTest, ShutdownCancelsAStartAtTheActorBoundary)
{
    // 两百点在未取消时需要约两秒，用于暴露 shutdown 与待处理 Start 的竞态。
    hardware_->prepareAsync(repeatedPlan(plan_, 200));
    ASSERT_TRUE(events_.waitPrepared(3));
    const vna::ConfigurationId longConfiguration = events_.configuration();

    // startAsync 返回后立即关闭：Start 要么被 Actor 清除，要么启动后被取消，都不得跑完全程。
    hardware_->startAsync(longConfiguration);
    const auto startedAt = std::chrono::steady_clock::now();
    manager_->shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - startedAt;
    EXPECT_LT(elapsed, std::chrono::seconds(1));
    manager_.reset();
}

}  // namespace
