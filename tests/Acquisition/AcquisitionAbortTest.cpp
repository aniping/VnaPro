#include "AcquisitionTestSupport.h"

namespace vna::test {

class AbortTerminalOrderTest : public AcquisitionTest, public ::testing::WithParamInterface<bool> {};

TEST_P(AbortTerminalOrderTest, PendingStartAbortWaitsForBothHardwareTerminals)
{
    ASSERT_NO_THROW(prepareSuccessfully(111));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Continuous, 0});
    ASSERT_TRUE(run.hasValue());
    EXPECT_EQ(engine_->requestAbort(run.value()).code, VnaErrorCode::None);
    EXPECT_EQ(hardware_->abortCallCount(), 0U);

    const SweepId sweepId = 1101;
    hardware_->publishStartSucceeded(hardware_->lastStartCommand(), sweepId);
    ASSERT_TRUE(hardware_->waitForAbortCalls(1));
    const CommandId abortCommand = hardware_->lastAbortCommand();
    const SweepCompletion terminal{
        sweepId, SweepTerminationReason::Aborted, sink_.prepared(0).prepared->pointExcitationRecordCount(), 0, 0};

    if (GetParam()) {
        hardware_->publishSweepFinished(terminal, DeviceState::Prepared);
        hardware_->publishAbortSucceeded(abortCommand, sweepId);
    } else {
        hardware_->publishAbortSucceeded(abortCommand, sweepId);
        hardware_->publishSweepFinished(terminal, DeviceState::Prepared);
    }

    ASSERT_TRUE(sink_.waitForCompletions(1));
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.runId, run.value());
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Aborted);
    EXPECT_EQ(completion.completedSweepCount, 0U);
    EXPECT_FALSE(completion.error.has_value());
    EXPECT_EQ(sink_.frameCount(), 0U);
    EXPECT_EQ(sink_.completionCount(), 1U);
    EXPECT_EQ(hardware_->abortCallCount(), 1U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
}

INSTANTIATE_TEST_SUITE_P(
    SweepFirstAndCommandFirst, AbortTerminalOrderTest, ::testing::Values(true, false),
    [](const ::testing::TestParamInfo<bool> &information) {
        return information.param ? "SweepFirst" : "CommandFirst";
    });

TEST_F(AcquisitionTest, UnsupportedAbortLeavesRunUnaffected)
{
    ASSERT_NO_THROW(resetEngine(false, spaciousLimits()));
    ASSERT_NO_THROW(prepareSuccessfully(112));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());

    const VnaError rejected = engine_->requestAbort(run.value());
    EXPECT_EQ(rejected.code, VnaErrorCode::UnsupportedPlan);
    EXPECT_EQ(hardware_->abortCallCount(), 0U);

    ASSERT_TRUE(completeCurrentSweep(1102));
    ASSERT_TRUE(sink_.waitForCompletions(1));
    EXPECT_EQ(sink_.completion(0).termination, AcquisitionRunTermination::Completed);
    EXPECT_EQ(sink_.completion(0).completedSweepCount, 1U);
    EXPECT_EQ(sink_.frameCount(), 1U);
    EXPECT_EQ(hardware_->abortCallCount(), 0U);
}

TEST_F(AcquisitionTest, PendingStartAbortStillDrainsAfterAnInterveningProtocolFault)
{
    ASSERT_NO_THROW(prepareSuccessfully(113));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Continuous, 0});
    ASSERT_TRUE(run.hasValue());
    EXPECT_EQ(engine_->requestAbort(run.value()).code, VnaErrorCode::None);

    hardware_->publishAbortSucceeded(999, 999);
    const SweepId sweepId = 1103;
    hardware_->publishStartSucceeded(hardware_->lastStartCommand(), sweepId);
    ASSERT_TRUE(hardware_->waitForAbortCalls(1));
    const CommandId abortCommand = hardware_->lastAbortCommand();
    const auto recordCount = sink_.prepared(0).prepared->pointExcitationRecordCount();
    hardware_->publishSweepFinished(
        SweepCompletion{sweepId, SweepTerminationReason::Aborted, recordCount, 0, 0}, DeviceState::Prepared);
    hardware_->publishAbortSucceeded(abortCommand, sweepId);

    ASSERT_TRUE(sink_.waitForCompletions(1));
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.runId, run.value());
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Failed);
    ASSERT_TRUE(completion.error.has_value());
    EXPECT_EQ(completion.error->code, VnaErrorCode::ProtocolError);
    EXPECT_EQ(hardware_->abortCallCount(), 1U);
    EXPECT_EQ(sink_.completionCount(), 1U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Fault);
}

}  // namespace vna::test
