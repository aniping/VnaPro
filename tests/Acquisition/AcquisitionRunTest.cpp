#include "AcquisitionTestSupport.h"

namespace vna::test {

TEST_F(AcquisitionTest, FiniteOnePublishesOneFrameAndOneCompletion)
{
    ASSERT_NO_THROW(prepareSuccessfully(101));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());

    ASSERT_TRUE(completeCurrentSweep(1001));
    ASSERT_TRUE(sink_.waitForFrames(1));
    ASSERT_TRUE(sink_.waitForCompletions(1));

    EXPECT_EQ(sink_.frame(0)->sweepIndex(), 0U);
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.runId, run.value());
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Completed);
    EXPECT_EQ(completion.completedSweepCount, 1U);
    EXPECT_EQ(sink_.completionCount(), 1U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
}

TEST_F(AcquisitionTest, FiniteTwoStartsAndPublishesExactlyTwoSweeps)
{
    ASSERT_NO_THROW(prepareSuccessfully(102));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 2});
    ASSERT_TRUE(run.hasValue());

    ASSERT_TRUE(completeCurrentSweep(1002));
    ASSERT_TRUE(sink_.waitForFrames(1));
    ASSERT_TRUE(hardware_->waitForStartCalls(2));
    EXPECT_EQ(sink_.completionCount(), 0U);

    ASSERT_TRUE(completeCurrentSweep(1003));
    ASSERT_TRUE(sink_.waitForFrames(2));
    ASSERT_TRUE(sink_.waitForCompletions(1));

    EXPECT_EQ(sink_.frame(0)->sweepIndex(), 0U);
    EXPECT_EQ(sink_.frame(1)->sweepIndex(), 1U);
    EXPECT_EQ(sink_.frame(0)->runId(), run.value());
    EXPECT_EQ(sink_.frame(1)->runId(), run.value());
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Completed);
    EXPECT_EQ(completion.completedSweepCount, 2U);
    EXPECT_EQ(hardware_->startCallCount(), 2U);
    EXPECT_EQ(sink_.completionCount(), 1U);
}

TEST_F(AcquisitionTest, ContinuousStopLetsCurrentSweepFinishAndDoesNotStartAnother)
{
    ASSERT_NO_THROW(prepareSuccessfully(103));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Continuous, 0});
    ASSERT_TRUE(run.hasValue());
    EXPECT_EQ(engine_->requestStop(run.value()).code, VnaErrorCode::None);
    EXPECT_EQ(engine_->requestStop(run.value()).code, VnaErrorCode::None);

    ASSERT_TRUE(completeCurrentSweep(1004));
    ASSERT_TRUE(sink_.waitForFrames(1));
    ASSERT_TRUE(sink_.waitForCompletions(1));

    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Stopped);
    EXPECT_EQ(completion.completedSweepCount, 1U);
    EXPECT_EQ(hardware_->startCallCount(), 1U);
    EXPECT_EQ(sink_.completionCount(), 1U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
}

}  // namespace vna::test
