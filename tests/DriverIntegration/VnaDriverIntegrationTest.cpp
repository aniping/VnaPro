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
        const vna::CommandId connectCommand = vna::test::unwrapCommand(hardware_->connectAsync());
        ASSERT_TRUE(events_.waitConnected());
        EXPECT_TRUE(events_.completedCommand(connectCommand));
        EXPECT_TRUE(hardware_->capabilities().supportsInternalTrigger);
        EXPECT_FALSE(hardware_->capabilities().supportsExternalTrigger);
        EXPECT_TRUE(hardware_->capabilities().perPointIfBandwidth);
        EXPECT_FALSE(hardware_->capabilities().perPointDwell);

        // 重复准备产生新 ConfigurationId，旧代次必须被拒绝。
        plan_ = vna::test::makePlan();
        ASSERT_TRUE(vna::isValid(hardware_->validate(plan_)));
        const vna::CommandId firstPrepare = vna::test::unwrapCommand(hardware_->prepareAsync(plan_));
        ASSERT_TRUE(events_.waitPrepared(1));
        EXPECT_TRUE(events_.completedCommand(firstPrepare));
        const vna::ConfigurationId expiredConfiguration = events_.configuration();
        const vna::CommandId secondPrepare = vna::test::unwrapCommand(hardware_->prepareAsync(plan_));
        ASSERT_TRUE(events_.waitPrepared(2));
        EXPECT_TRUE(events_.completedCommand(secondPrepare));
        configuration_ = events_.configuration();
        const vna::CommandId expiredStart = vna::test::unwrapCommand(hardware_->startAsync(expiredConfiguration));
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

    std::chrono::steady_clock::duration runTimedSweep(
        double ifBandwidthHz, std::size_t prepareOrdinal, std::size_t completionOrdinal)
    {
        auto timedPlan = repeatedPlan(plan_, 300);
        for (auto &point : timedPlan.points) {
            point.ifBandwidthHz = ifBandwidthHz;
            point.dwellTime = std::chrono::nanoseconds::zero();
        }
        const vna::CommandId prepareCommand = vna::test::unwrapCommand(hardware_->prepareAsync(std::move(timedPlan)));
        if (!events_.waitPrepared(prepareOrdinal)) {
            ADD_FAILURE() << "等待计时计划 Prepare 超时";
            return std::chrono::steady_clock::duration::max();
        }
        EXPECT_TRUE(events_.completedCommand(prepareCommand));
        const auto startedAt = std::chrono::steady_clock::now();
        const vna::CommandId startCommand = vna::test::unwrapCommand(hardware_->startAsync(events_.configuration()));
        if (!events_.waitCompleted(completionOrdinal)) {
            ADD_FAILURE() << "等待计时扫描完成超时";
            return std::chrono::steady_clock::duration::max();
        }
        EXPECT_TRUE(events_.completedCommand(startCommand));
        EXPECT_EQ(events_.completion(completionOrdinal - 1).reason, vna::SweepTerminationReason::Completed);
        return std::chrono::steady_clock::now() - startedAt;
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
    // 完整扫频验证批次、顺序号、开路波模型和有效质量位。
    const vna::CommandId completedStart = vna::test::unwrapCommand(hardware_->startAsync(configuration_));
    ASSERT_TRUE(events_.waitCompleted(1));
    EXPECT_TRUE(events_.completedCommand(completedStart));
    vna::test::verifyCompletedSweep(events_);
    const auto initialIncident = events_.blockValue(0, 0, 0);
    const auto initialNoiseFloor = events_.blockValue(0, 1, 0);
    events_.clearBlocks();

    // 在 StartCompleted 回调内立即中止，覆盖 Worker 尚未进入 Execute 的交接竞态。
    vna::test::unwrapCommand(hardware_->prepareAsync(repeatedPlan(plan_, 50)));
    ASSERT_TRUE(events_.waitPrepared(3));
    const vna::ConfigurationId abortConfiguration = events_.configuration();
    events_.abortOnNextStart(*hardware_);
    const vna::CommandId abortedStart = vna::test::unwrapCommand(hardware_->startAsync(abortConfiguration));
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

    // 中止标志只属于一次 SweepId，后续配置仍可完整执行。
    vna::test::unwrapCommand(hardware_->prepareAsync(plan_));
    ASSERT_TRUE(events_.waitPrepared(4));
    configuration_ = events_.configuration();
    const vna::CommandId repeatedStart = vna::test::unwrapCommand(hardware_->startAsync(configuration_));
    ASSERT_TRUE(events_.waitCompleted(3));
    EXPECT_EQ(events_.completion(2).reason, vna::SweepTerminationReason::Completed);
    EXPECT_TRUE(events_.completedCommand(repeatedStart));
    EXPECT_NE(events_.blockValue(0, 0, 0), initialIncident);
    EXPECT_NE(events_.blockValue(0, 1, 0), initialNoiseFloor);

    // 在所有波块释放后断开，验证完整生命周期收尾；manager 由 TearDown 关闭。
    const vna::CommandId disconnectCommand = vna::test::unwrapCommand(hardware_->disconnectAsync());
    ASSERT_TRUE(events_.waitState(vna::DeviceState::Disconnected));
    ASSERT_TRUE(events_.waitDisconnected(1));
    EXPECT_TRUE(events_.disconnectFollowedState());
    EXPECT_TRUE(events_.completedCommand(disconnectCommand));
}

TEST_F(VnaDriverIntegrationTest, ShutdownCancelsAStartAtTheActorBoundary)
{
    // 100 Hz 下两百点在未取消时需要约两秒，用于暴露 shutdown 与待处理 Start 的竞态。
    auto longPlan = repeatedPlan(plan_, 200);
    for (auto &point : longPlan.points) {
        point.ifBandwidthHz = 100.0;
    }
    vna::test::unwrapCommand(hardware_->prepareAsync(std::move(longPlan)));
    ASSERT_TRUE(events_.waitPrepared(3));
    const vna::ConfigurationId longConfiguration = events_.configuration();

    // startAsync 返回后立即关闭：Start 要么被 Actor 清除，要么启动后被取消，都不得跑完全程。
    vna::test::unwrapCommand(hardware_->startAsync(longConfiguration));
    const auto startedAt = std::chrono::steady_clock::now();
    manager_->shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - startedAt;
    EXPECT_LT(elapsed, std::chrono::seconds(1));
    manager_.reset();
}

TEST_F(VnaDriverIntegrationTest, AbortInterruptsANarrowIfBandwidthWait)
{
    auto narrowPlan = repeatedPlan(plan_, 4);
    for (auto &point : narrowPlan.points) {
        point.dwellTime = std::chrono::nanoseconds::zero();
        point.ifBandwidthHz = 1.0;
    }
    narrowPlan.points[0].ifBandwidthHz = 10.0e6;
    narrowPlan.points[1].ifBandwidthHz = 10.0e6;
    const vna::CommandId prepareCommand = vna::test::unwrapCommand(hardware_->prepareAsync(std::move(narrowPlan)));
    ASSERT_TRUE(events_.waitPrepared(3));
    EXPECT_TRUE(events_.completedCommand(prepareCommand));

    const vna::CommandId startCommand = vna::test::unwrapCommand(hardware_->startAsync(events_.configuration()));
    ASSERT_TRUE(events_.waitStarted(1));
    ASSERT_TRUE(events_.waitBlocks(1));
    const auto abortedAt = std::chrono::steady_clock::now();
    const vna::CommandId abortCommand = vna::test::unwrapCommand(hardware_->abortAsync(events_.sweep(0)));
    ASSERT_TRUE(events_.waitCompleted(1));
    ASSERT_TRUE(events_.waitAborted(1));

    EXPECT_LT(std::chrono::steady_clock::now() - abortedAt, std::chrono::milliseconds(500));
    EXPECT_EQ(events_.completion(0).reason, vna::SweepTerminationReason::Aborted);
    EXPECT_EQ(events_.blockCount(), 1U);
    EXPECT_TRUE(events_.completedCommand(startCommand));
    EXPECT_TRUE(events_.completedCommand(abortCommand));
    events_.clearBlocks();
}

TEST_F(VnaDriverIntegrationTest, TenToThirtyKilohertzHasNoFixedBatchThrottle)
{
    events_.discardFutureBlocks();
    const std::size_t batchPoints = hardware_->capabilities().preferredBatchPoints;
    ASSERT_GT(batchPoints, 0U);
    const std::size_t expectedBlocks = (300U + batchPoints - 1U) / batchPoints;

    const auto tenKilohertz = runTimedSweep(10000.0, 3, 1);
    ASSERT_NE(tenKilohertz, std::chrono::steady_clock::duration::max());
    EXPECT_EQ(events_.blockCount(), expectedBlocks);
    events_.clearBlocks();
    const auto thirtyKilohertz = runTimedSweep(30000.0, 4, 2);
    ASSERT_NE(thirtyKilohertz, std::chrono::steady_clock::duration::max());
    EXPECT_EQ(events_.blockCount(), expectedBlocks);

    // 墙钟上限只拦截人为节流；精确的 100/33.334 us 模拟器逻辑窗口由纯函数测试验证。
    EXPECT_LT(tenKilohertz, std::chrono::milliseconds(500));
    EXPECT_LT(thirtyKilohertz, std::chrono::milliseconds(500));
    EXPECT_LT(thirtyKilohertz, tenKilohertz + std::chrono::milliseconds(100));
}

TEST_F(VnaDriverIntegrationTest, RejectsAPlanThatWouldOccupyTheSimulatorForOverOneHour)
{
    auto excessivePlan = repeatedPlan(plan_, 1);
    excessivePlan.points.front().ifBandwidthHz = 1.0;
    excessivePlan.points.front().dwellTime = std::chrono::hours(1);

    const vna::CommandId prepareCommand = vna::test::unwrapCommand(hardware_->prepareAsync(std::move(excessivePlan)));
    ASSERT_TRUE(events_.waitFailures(2));
    EXPECT_TRUE(events_.failedCommand(prepareCommand));
}

}  // namespace
