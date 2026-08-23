#include "AcquisitionTestSupport.h"

namespace vna::test {

TEST_F(AcquisitionTest, RequestedPayloadBudgetsAcceptExactLimits)
{
    const AcquisitionLimits exact = exactLimitsFor(plan_);
    ASSERT_GT(exact.maximumRawFramePayloadBytes, 0U);
    ASSERT_GT(exact.maximumPreparedMappingPayloadBytes, 0U);
    ASSERT_NO_THROW(resetEngine(true, exact));

    auto submitted = engine_->prepareAsync(plan_);
    ASSERT_TRUE(submitted.hasValue());
    EXPECT_EQ(hardware_->validationCallCount(), 1U);
    EXPECT_EQ(hardware_->prepareCallCount(), 1U);

    prepared_ = makeSparsePrepared(plan_, 51);
    hardware_->publishPrepareSucceeded(submitted.value(), prepared_);
    ASSERT_TRUE(sink_.waitForPrepared(1));
    EXPECT_NE(sink_.prepared(0).prepared, nullptr);
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
}

TEST_F(AcquisitionTest, RawPayloadOneByteShortRejectsBeforeHardware)
{
    AcquisitionLimits limits = exactLimitsFor(plan_);
    --limits.maximumRawFramePayloadBytes;
    ASSERT_NO_THROW(resetEngine(true, limits));

    const auto submitted = engine_->prepareAsync(plan_);

    ASSERT_FALSE(submitted.hasValue());
    EXPECT_EQ(submitted.error().code, VnaErrorCode::UnsupportedPlan);
    EXPECT_EQ(hardware_->validationCallCount(), 0U);
    EXPECT_EQ(hardware_->prepareCallCount(), 0U);
    EXPECT_EQ(sink_.preparedCount(), 0U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Unprepared);
}

TEST_F(AcquisitionTest, MappingPayloadOneByteShortRejectsBeforeHardware)
{
    AcquisitionLimits limits = exactLimitsFor(plan_);
    --limits.maximumPreparedMappingPayloadBytes;
    ASSERT_NO_THROW(resetEngine(true, limits));

    const auto submitted = engine_->prepareAsync(plan_);

    ASSERT_FALSE(submitted.hasValue());
    EXPECT_EQ(submitted.error().code, VnaErrorCode::UnsupportedPlan);
    EXPECT_EQ(hardware_->validationCallCount(), 0U);
    EXPECT_EQ(hardware_->prepareCallCount(), 0U);
    EXPECT_EQ(sink_.preparedCount(), 0U);
}

TEST_F(AcquisitionTest, FirstPrepareFailureLeavesEngineUnprepared)
{
    const auto submitted = engine_->prepareAsync(plan_);
    ASSERT_TRUE(submitted.hasValue());

    hardware_->publishCommandFailed(
        submitted.value(), testError(VnaErrorCode::ConfigurationRejected, "injected prepare rejection"));

    ASSERT_TRUE(sink_.waitForPrepared(1));
    const auto notice = sink_.prepared(0);
    EXPECT_EQ(notice.commandId, submitted.value());
    EXPECT_EQ(notice.prepared, nullptr);
    EXPECT_EQ(notice.error.code, VnaErrorCode::ConfigurationRejected);
    EXPECT_EQ(engine_->state(), AcquisitionState::Unprepared);
    EXPECT_EQ(sink_.completionCount(), 0U);
}

TEST_F(AcquisitionTest, FailedReprepareKeepsPreviousConfigurationUsable)
{
    ASSERT_NO_THROW(prepareSuccessfully(71));
    const auto original = sink_.prepared(0).prepared;
    auto reprepare = engine_->prepareAsync(plan_);
    ASSERT_TRUE(reprepare.hasValue());

    hardware_->publishCommandFailed(
        reprepare.value(), testError(VnaErrorCode::ConfigurationRejected, "injected reprepare rejection"));

    ASSERT_TRUE(sink_.waitForPrepared(2));
    EXPECT_EQ(sink_.prepared(1).prepared, nullptr);
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
    auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());
    EXPECT_EQ(hardware_->lastStartConfiguration(), 71U);
    ASSERT_TRUE(completeCurrentSweep(801));
    ASSERT_TRUE(sink_.waitForCompletions(1));
    EXPECT_EQ(
        sink_.frame(0)->prepared().hardwarePrepared().configurationId, original->hardwarePrepared().configurationId);
}

TEST_F(AcquisitionTest, FailedLocalCompilationClearsPreviousConfiguration)
{
    ASSERT_NO_THROW(prepareSuccessfully(81));
    auto reprepare = engine_->prepareAsync(plan_);
    ASSERT_TRUE(reprepare.hasValue());
    PreparedSweep malformed = makeSparsePrepared(plan_, 82);
    malformed.layouts.clear();

    hardware_->publishPrepareSucceeded(reprepare.value(), std::move(malformed));

    ASSERT_TRUE(sink_.waitForPrepared(2));
    EXPECT_EQ(sink_.prepared(1).prepared, nullptr);
    EXPECT_NE(sink_.prepared(1).error.code, VnaErrorCode::None);
    EXPECT_EQ(engine_->state(), AcquisitionState::Unprepared);
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_FALSE(run.hasValue());
    EXPECT_EQ(run.error().code, VnaErrorCode::InvalidState);
}

TEST_F(AcquisitionTest, FaultedReprepareClearsPreviousConfiguration)
{
    ASSERT_NO_THROW(prepareSuccessfully(83));
    const auto reprepare = engine_->prepareAsync(plan_);
    ASSERT_TRUE(reprepare.hasValue());

    hardware_->publishState(DeviceState::Fault);
    hardware_->publishCommandFailed(
        reprepare.value(), testError(VnaErrorCode::HardwareFault, "injected prepare fault"));

    ASSERT_TRUE(sink_.waitForPrepared(2));
    EXPECT_EQ(sink_.prepared(1).prepared, nullptr);
    EXPECT_EQ(sink_.prepared(1).error.code, VnaErrorCode::HardwareFault);
    EXPECT_EQ(engine_->state(), AcquisitionState::Fault);
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_FALSE(run.hasValue());
    EXPECT_EQ(run.error().code, VnaErrorCode::InvalidState);
}

}  // namespace vna::test
