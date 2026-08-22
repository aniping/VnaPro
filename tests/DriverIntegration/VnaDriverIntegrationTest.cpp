#include <gtest/gtest.h>

#include <memory>

#include "VnaIntegrationTestSupport.h"

namespace {

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
        hardware_->connectAsync();
        ASSERT_TRUE(events_.waitConnected());

        // 重复准备产生新 ConfigurationId，旧代次必须被拒绝。
        plan_ = vna::test::makePlan();
        ASSERT_TRUE(vna::isValid(hardware_->validate(plan_)));
        hardware_->prepareAsync(plan_);
        ASSERT_TRUE(events_.waitPrepared(1));
        const vna::ConfigurationId expiredConfiguration = events_.configuration();
        hardware_->prepareAsync(plan_);
        ASSERT_TRUE(events_.waitPrepared(2));
        configuration_ = events_.configuration();
        hardware_->startAsync(expiredConfiguration);
        ASSERT_TRUE(events_.waitFailures(1));
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
    hardware_->startAsync(configuration_);
    ASSERT_TRUE(events_.waitCompleted(1));
    vna::test::verifyCompletedSweep(events_);
    events_.clearBlocks();

    // 运行中 Prepare 被拒绝，中止请求产生 Aborted 而不是 HardwareFault。
    hardware_->startAsync(configuration_);
    ASSERT_TRUE(events_.waitStarted(2));
    hardware_->prepareAsync(plan_);
    hardware_->abortAsync(events_.sweep(1));
    ASSERT_TRUE(events_.waitCompleted(2));
    ASSERT_TRUE(events_.waitFailures(2));
    EXPECT_EQ(events_.completion(1).reason, vna::SweepTerminationReason::Aborted);
    EXPECT_EQ(events_.failureCount(), 2U);
    events_.clearBlocks();

    // 中止标志只属于一次 SweepId，同一配置随后仍可完整执行。
    hardware_->startAsync(configuration_);
    ASSERT_TRUE(events_.waitCompleted(3));
    EXPECT_EQ(events_.completion(2).reason, vna::SweepTerminationReason::Completed);

    // 在所有波块释放后断开，验证完整生命周期收尾；manager 由 TearDown 关闭。
    hardware_->disconnectAsync();
    EXPECT_TRUE(events_.waitState(vna::DeviceState::Disconnected));
}

}  // namespace
