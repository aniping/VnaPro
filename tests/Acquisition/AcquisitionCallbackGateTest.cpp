#include "AcquisitionTestSupport.h"

#include <atomic>
#include <chrono>
#include <future>

namespace vna::test {

TEST_F(AcquisitionTest, PreparedCallbackKeepsReadyStateUntilExternalStartCanProceed)
{
    CallbackBarrier callback;
    std::atomic<AcquisitionState> stateAtEntry{AcquisitionState::Fault};
    std::atomic<AcquisitionState> stateBeforeReturn{AcquisitionState::Fault};
    sink_.setPreparedEntryHook([this, &callback, &stateAtEntry, &stateBeforeReturn] {
        stateAtEntry.store(engine_->state());
        callback.arriveAndWait();
        stateBeforeReturn.store(engine_->state());
    });
    prepared_ = makeSparsePrepared(plan_, 131);
    const auto prepare = engine_->prepareAsync(plan_);
    ASSERT_TRUE(prepare.hasValue());
    hardware_->publishPrepareSucceeded(prepare.value(), prepared_);
    ASSERT_TRUE(callback.waitUntilArrived());

    std::promise<void> externalStarted;
    auto externalStart = std::async(std::launch::async, [this, &externalStarted] {
        externalStarted.set_value();
        return engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    });
    externalStarted.get_future().wait();
    const auto statusWhileBlocked = externalStart.wait_for(std::chrono::milliseconds(100));
    callback.release();
    auto run = externalStart.get();

    EXPECT_EQ(statusWhileBlocked, std::future_status::timeout);
    ASSERT_TRUE(run.hasValue());
    ASSERT_TRUE(sink_.waitForPrepared(1));
    EXPECT_EQ(stateAtEntry.load(), AcquisitionState::Ready);
    EXPECT_EQ(stateBeforeReturn.load(), AcquisitionState::Ready);
    ASSERT_TRUE(completeCurrentSweep(1301));
    ASSERT_TRUE(sink_.waitForCompletions(1));
}

TEST_F(AcquisitionTest, RunCompletionCallbackAllowsReentrantStateButGatesExternalState)
{
    ASSERT_NO_THROW(prepareSuccessfully(132));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());
    CallbackBarrier callback;
    std::atomic<AcquisitionState> stateAtEntry{AcquisitionState::Fault};
    std::atomic<AcquisitionState> stateBeforeReturn{AcquisitionState::Fault};
    sink_.setCompletionEntryHook([this, &callback, &stateAtEntry, &stateBeforeReturn] {
        stateAtEntry.store(engine_->state());
        callback.arriveAndWait();
        stateBeforeReturn.store(engine_->state());
    });
    ASSERT_TRUE(completeCurrentSweep(1302));
    ASSERT_TRUE(callback.waitUntilArrived());

    std::promise<void> externalStarted;
    auto externalState = std::async(std::launch::async, [this, &externalStarted] {
        externalStarted.set_value();
        return engine_->state();
    });
    externalStarted.get_future().wait();
    const auto statusWhileBlocked = externalState.wait_for(std::chrono::milliseconds(100));
    callback.release();
    const AcquisitionState observed = externalState.get();

    EXPECT_EQ(statusWhileBlocked, std::future_status::timeout);
    EXPECT_EQ(observed, AcquisitionState::Ready);
    ASSERT_TRUE(sink_.waitForCompletions(1));
    EXPECT_EQ(stateAtEntry.load(), AcquisitionState::Ready);
    EXPECT_EQ(stateBeforeReturn.load(), AcquisitionState::Ready);
}

TEST_F(AcquisitionTest, RepeatedStopIsAcceptedAfterSweepCompletionIsLatched)
{
    ASSERT_NO_THROW(prepareSuccessfully(133));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Continuous, 0});
    ASSERT_TRUE(run.hasValue());
    EXPECT_EQ(engine_->requestStop(run.value()).code, VnaErrorCode::None);
    CallbackBarrier callback;
    sink_.setFrameEntryHook([&callback] { callback.arriveAndWait(); });

    ASSERT_TRUE(completeCurrentSweep(1303));
    ASSERT_TRUE(callback.waitUntilArrived());
    const VnaError repeatedStop = engine_->requestStop(run.value());
    callback.release();

    EXPECT_EQ(repeatedStop.code, VnaErrorCode::None);
    ASSERT_TRUE(sink_.waitForCompletions(1));
    EXPECT_EQ(sink_.completion(0).termination, AcquisitionRunTermination::Stopped);
    EXPECT_EQ(sink_.completionCount(), 1U);
}

}  // namespace vna::test
