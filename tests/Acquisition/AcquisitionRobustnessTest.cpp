#include "AcquisitionTestSupport.h"

namespace vna::test {
namespace {

    enum class InvalidBlockPattern { Replay, Overlap, OutOfOrder, SequenceGap };

    std::vector<WaveBlockSpec> invalidBlocks(const VnaSweepPlan &plan, SweepId sweepId, InvalidBlockPattern pattern)
    {
        auto blocks = makeSparseBlocks(plan, sweepId);
        if (pattern == InvalidBlockPattern::Replay) {
            WaveBlockSpec replay = blocks.front();
            replay.sequence = 1;
            return {blocks.front(), std::move(replay)};
        }
        if (pattern == InvalidBlockPattern::Overlap) {
            WaveBlockSpec first = blocks.back();
            WaveBlockSpec overlap = blocks.back();
            first.sequence = 0;
            overlap.sequence = 1;
            overlap.firstPointIndex = 1;
            for (auto &wave : first.waves) {
                wave.resize(2);
            }
            for (auto &wave : overlap.waves) {
                wave.erase(wave.begin());
                wave.resize(2);
            }
            return {std::move(first), std::move(overlap)};
        }
        WaveBlockSpec invalid = pattern == InvalidBlockPattern::OutOfOrder ? blocks[1] : blocks[0];
        invalid.sequence = pattern == InvalidBlockPattern::SequenceGap ? 1 : 0;
        return {std::move(invalid)};
    }

    const char *patternName(InvalidBlockPattern pattern)
    {
        switch (pattern) {
            case InvalidBlockPattern::Replay:
                return "Replay";
            case InvalidBlockPattern::Overlap:
                return "Overlap";
            case InvalidBlockPattern::OutOfOrder:
                return "OutOfOrder";
            case InvalidBlockPattern::SequenceGap:
                return "SequenceGap";
        }
        return "Unknown";
    }

}  // namespace

class InvalidWaveBlockCoverageTest : public AcquisitionTest,
                                     public ::testing::WithParamInterface<InvalidBlockPattern> {};

TEST_P(InvalidWaveBlockCoverageTest, RejectsInvalidCoverageWithoutPublishingAFrame)
{
    ASSERT_NO_THROW(prepareSuccessfully(120));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());
    const SweepId sweepId = 1200;
    hardware_->publishStartSucceeded(hardware_->lastStartCommand(), sweepId);
    for (const auto &block : invalidBlocks(plan_, sweepId, GetParam())) {
        ASSERT_TRUE(hardware_->publishWaveBlock(block));
    }

    ASSERT_TRUE(hardware_->waitForAbortCalls(1));
    const CommandId abortCommand = hardware_->lastAbortCommand();
    const std::size_t records = sink_.prepared(0).prepared->pointExcitationRecordCount();
    hardware_->publishSweepFinished(
        SweepCompletion{sweepId, SweepTerminationReason::Aborted, records, 0, 0}, DeviceState::Prepared);
    hardware_->publishAbortSucceeded(abortCommand, sweepId);

    ASSERT_TRUE(sink_.waitForCompletions(1));
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.runId, run.value());
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Failed);
    ASSERT_TRUE(completion.error.has_value());
    EXPECT_EQ(completion.error->code, VnaErrorCode::ProtocolError);
    EXPECT_EQ(sink_.frameCount(), 0U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Fault);
}

INSTANTIATE_TEST_SUITE_P(
    ReplayOverlapOrderAndSequence, InvalidWaveBlockCoverageTest,
    ::testing::Values(
        InvalidBlockPattern::Replay, InvalidBlockPattern::Overlap, InvalidBlockPattern::OutOfOrder,
        InvalidBlockPattern::SequenceGap),
    [](const ::testing::TestParamInfo<InvalidBlockPattern> &information) { return patternName(information.param); });

TEST_F(AcquisitionTest, PreStartCompletionThenStartFailureCompletesRunOnce)
{
    ASSERT_NO_THROW(prepareSuccessfully(121));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());
    const CommandId startCommand = hardware_->lastStartCommand();

    hardware_->publishSweepFinished(
        SweepCompletion{1201, SweepTerminationReason::HardwareFault, 7, 0, 0}, DeviceState::Fault);
    hardware_->publishCommandFailed(
        startCommand, testError(VnaErrorCode::HardwareFault, "injected pre-start routing failure"));

    ASSERT_TRUE(sink_.waitForCompletions(1));
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.runId, run.value());
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Failed);
    EXPECT_EQ(completion.completedSweepCount, 0U);
    ASSERT_TRUE(completion.error.has_value());
    EXPECT_EQ(completion.error->code, VnaErrorCode::HardwareFault);
    EXPECT_EQ(sink_.completionCount(), 1U);
    EXPECT_EQ(sink_.frameCount(), 0U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Fault);
}

TEST_F(AcquisitionTest, SinkExceptionsDoNotDisableHardwareSubscription)
{
    sink_.throwOnNextPrepared();
    ASSERT_NO_THROW(prepareSuccessfully(122));
    sink_.throwOnNextFrame();
    sink_.throwOnNextCompletion();
    auto firstRun = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(firstRun.hasValue());
    ASSERT_TRUE(completeCurrentSweep(1202));
    ASSERT_TRUE(sink_.waitForCompletions(1));

    auto secondRun = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(secondRun.hasValue());
    ASSERT_TRUE(completeCurrentSweep(1203));
    ASSERT_TRUE(sink_.waitForFrames(2));
    ASSERT_TRUE(sink_.waitForCompletions(2));

    EXPECT_NE(firstRun.value(), secondRun.value());
    EXPECT_EQ(sink_.completion(0).termination, AcquisitionRunTermination::Completed);
    EXPECT_EQ(sink_.completion(1).termination, AcquisitionRunTermination::Completed);
    EXPECT_EQ(sink_.frame(0)->runId(), firstRun.value());
    EXPECT_EQ(sink_.frame(1)->runId(), secondRun.value());
    EXPECT_EQ(sink_.completionCount(), 2U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
}

}  // namespace vna::test
