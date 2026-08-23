#include "AcquisitionTestSupport.h"

#include <memory>

namespace vna::test {
namespace {

    VnaError exhausted(const char *description)
    {
        return testError(VnaErrorCode::ResourceExhausted, description);
    }

}  // namespace

TEST_F(AcquisitionTest, SynchronousPrepareRejectionKeepsPreviousConfiguration)
{
    ASSERT_NO_THROW(prepareSuccessfully(141));
    const std::size_t preparedCount = sink_.preparedCount();
    hardware_->rejectNextPrepare(exhausted("prepare queue is full"));

    const auto rejected = engine_->prepareAsync(plan_);

    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, VnaErrorCode::ResourceExhausted);
    EXPECT_EQ(rejected.error().description, "prepare queue is full");
    EXPECT_EQ(sink_.preparedCount(), preparedCount);
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());
    EXPECT_EQ(hardware_->lastStartConfiguration(), 141U);
    ASSERT_TRUE(completeCurrentSweep(1401));
    ASSERT_TRUE(sink_.waitForCompletions(1));
}

TEST_F(AcquisitionTest, SynchronousStartRejectionDoesNotConsumeRunIdentifier)
{
    ASSERT_NO_THROW(prepareSuccessfully(142));
    hardware_->rejectNextStart(exhausted("start queue is full"));

    const auto rejected = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});

    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, VnaErrorCode::ResourceExhausted);
    EXPECT_EQ(rejected.error().description, "start queue is full");
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
    EXPECT_EQ(sink_.completionCount(), 0U);
    const auto accepted = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(accepted.hasValue());
    EXPECT_EQ(accepted.value(), 1U);
    ASSERT_TRUE(completeCurrentSweep(1402));
    ASSERT_TRUE(sink_.waitForCompletions(1));
}

TEST_F(AcquisitionTest, DeferredAbortRejectionBecomesObservableRunFailure)
{
    ASSERT_NO_THROW(prepareSuccessfully(143));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Continuous, 0});
    ASSERT_TRUE(run.hasValue());
    ASSERT_EQ(engine_->requestAbort(run.value()).code, VnaErrorCode::None);
    hardware_->rejectNextAbort(exhausted("abort queue is full"));

    const SweepId sweepId = 1403;
    hardware_->publishStartSucceeded(hardware_->lastStartCommand(), sweepId);
    ASSERT_TRUE(hardware_->waitForAbortCalls(2));
    const CommandId cleanupAbort = hardware_->lastAbortCommand();
    hardware_->publishSweepFinished(
        SweepCompletion{sweepId, SweepTerminationReason::Aborted, 7, 0, 0}, DeviceState::Prepared);
    hardware_->publishAbortSucceeded(cleanupAbort, sweepId);

    ASSERT_TRUE(sink_.waitForCompletions(1));
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Failed);
    ASSERT_TRUE(completion.error.has_value());
    EXPECT_EQ(completion.error->code, VnaErrorCode::ResourceExhausted);
    EXPECT_EQ(completion.error->description, "abort queue is full");
    EXPECT_EQ(engine_->state(), AcquisitionState::Fault);
}

TEST_F(AcquisitionTest, NextSweepRejectionCompletesRunWithSubmissionError)
{
    ASSERT_NO_THROW(prepareSuccessfully(144));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 2});
    ASSERT_TRUE(run.hasValue());
    hardware_->rejectNextStart(exhausted("next start queue is full"));

    ASSERT_TRUE(completeCurrentSweep(1404));
    ASSERT_TRUE(sink_.waitForFrames(1));
    ASSERT_TRUE(sink_.waitForCompletions(1));

    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Failed);
    EXPECT_EQ(completion.completedSweepCount, 1U);
    ASSERT_TRUE(completion.error.has_value());
    EXPECT_EQ(completion.error->code, VnaErrorCode::ResourceExhausted);
    EXPECT_EQ(completion.error->description, "next start queue is full");
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
}

TEST_F(AcquisitionTest, PreparedEventSharesOneImmutableHardwareSnapshot)
{
    const auto submitted = engine_->prepareAsync(plan_);
    ASSERT_TRUE(submitted.hasValue());
    auto snapshot = std::make_shared<const PreparedSweep>(makeSparsePrepared(plan_, 145));
    const PreparedSweep *const expectedAddress = snapshot.get();

    hardware_->publishPrepareSnapshot(submitted.value(), snapshot);

    ASSERT_TRUE(sink_.waitForPrepared(1));
    const auto prepared = sink_.prepared(0).prepared;
    ASSERT_NE(prepared, nullptr);
    EXPECT_EQ(&prepared->hardwarePrepared(), expectedAddress);
    snapshot.reset();
    EXPECT_EQ(prepared->hardwarePrepared().configurationId, 145U);
}

TEST_F(AcquisitionTest, NullPreparedSnapshotFailsWithoutDereference)
{
    const auto submitted = engine_->prepareAsync(plan_);
    ASSERT_TRUE(submitted.hasValue());

    hardware_->publishPrepareSnapshot(submitted.value(), nullptr);

    ASSERT_TRUE(sink_.waitForPrepared(1));
    const auto notice = sink_.prepared(0);
    EXPECT_EQ(notice.prepared, nullptr);
    EXPECT_EQ(notice.error.code, VnaErrorCode::ProtocolError);
    EXPECT_EQ(engine_->state(), AcquisitionState::Unprepared);
}

}  // namespace vna::test
