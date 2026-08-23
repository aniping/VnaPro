#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

#include "VnaBufferPool.h"
#include "VnaFrameTracker.h"

namespace {

vna::VnaExcitationPlan excitation(vna::ExcitationId identifier)
{
    vna::VnaExcitationPlan result;
    result.excitationId = identifier;
    return result;
}

void appendPoint(vna::VnaSweepPlan &plan, std::initializer_list<vna::ExcitationId> excitations)
{
    vna::VnaPointPlan point;
    point.pointId = static_cast<vna::PointId>(plan.points.size() + 1);
    for (const auto identifier : excitations) {
        point.excitations.push_back(excitation(identifier));
    }
    plan.points.push_back(std::move(point));
}

vna::VnaSweepPlan sparseMultiExcitationPlan()
{
    // 布局 10 出现在点 0/1/3，布局 20 出现在点 0/2/3，用于验证稀疏多激励覆盖。
    vna::VnaSweepPlan plan;
    appendPoint(plan, {1, 2});
    appendPoint(plan, {1});
    appendPoint(plan, {2});
    appendPoint(plan, {1, 2});
    return plan;
}

std::vector<vna::VnaWaveLayout> layouts()
{
    return {vna::VnaWaveLayout{10, 1, {}}, vna::VnaWaveLayout{20, 2, {}}};
}

VnaStatusV1 acceptedBlock(vna::VnaWaveBlock)
{
    VnaStatusV1 result{};
    result.code = VNA_STATUS_OK;
    return result;
}

TEST(VnaFrameTrackerTest, TracksSparseMultiExcitationCoverage)
{
    vna::detail::VnaFrameTracker tracker(sparseMultiExcitationPlan(), layouts());

    EXPECT_EQ(tracker.expectedFrameCount(), 6U);
    EXPECT_EQ(tracker.reserve(10, 0, 2), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_EQ(tracker.reserve(20, 0, 1), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_EQ(tracker.reserve(20, 2, 2), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_EQ(tracker.reserve(10, 3, 1), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_EQ(tracker.reservedFrameCount(), 6U);
    EXPECT_TRUE(tracker.complete());
}

TEST(VnaFrameTrackerTest, RejectsUnknownLayoutAndInvalidPlanRange)
{
    vna::detail::VnaFrameTracker tracker(sparseMultiExcitationPlan(), layouts());

    EXPECT_EQ(tracker.reserve(99, 0, 1), vna::detail::VnaFrameReservationResult::UnknownLayout);
    EXPECT_EQ(tracker.reserve(10, 0, 0), vna::detail::VnaFrameReservationResult::InvalidRange);
    EXPECT_EQ(tracker.reserve(10, 4, 1), vna::detail::VnaFrameReservationResult::InvalidRange);
    EXPECT_EQ(tracker.reserve(10, 3, UINT32_MAX), vna::detail::VnaFrameReservationResult::InvalidRange);
    EXPECT_EQ(tracker.reservedFrameCount(), 0U);
}

TEST(VnaFrameTrackerTest, RejectsRangeAcrossLayoutGapWithoutPartialReservation)
{
    vna::detail::VnaFrameTracker tracker(sparseMultiExcitationPlan(), layouts());

    EXPECT_EQ(tracker.reserve(10, 1, 2), vna::detail::VnaFrameReservationResult::UnexpectedPoint);
    EXPECT_EQ(tracker.reservedFrameCount(), 0U);
    EXPECT_EQ(tracker.reserve(10, 0, 2), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_EQ(tracker.reserve(10, 3, 1), vna::detail::VnaFrameReservationResult::Reserved);
}

TEST(VnaFrameTrackerTest, RejectsOutOfOrderRangeWithoutPartialReservation)
{
    vna::detail::VnaFrameTracker tracker(sparseMultiExcitationPlan(), layouts());

    EXPECT_EQ(tracker.reserve(10, 3, 1), vna::detail::VnaFrameReservationResult::OutOfOrderFrame);
    EXPECT_EQ(tracker.reservedFrameCount(), 0U);
    EXPECT_EQ(tracker.reserve(10, 0, 2), vna::detail::VnaFrameReservationResult::Reserved);
}

TEST(VnaFrameTrackerTest, RejectsOverlapAndRollsBackOnlyCompleteReservation)
{
    vna::detail::VnaFrameTracker tracker(sparseMultiExcitationPlan(), layouts());

    EXPECT_EQ(tracker.reserve(10, 0, 1), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_FALSE(tracker.release(10, 0, 2));
    EXPECT_EQ(tracker.reservedFrameCount(), 1U);
    EXPECT_EQ(tracker.reserve(10, 1, 1), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_EQ(tracker.reserve(10, 1, 1), vna::detail::VnaFrameReservationResult::DuplicateFrame);
    EXPECT_EQ(tracker.reservedFrameCount(), 2U);
    EXPECT_TRUE(tracker.release(10, 0, 2));
    EXPECT_FALSE(tracker.release(10, 0, 2));
    EXPECT_EQ(tracker.reservedFrameCount(), 0U);
    EXPECT_EQ(tracker.reserve(10, 0, 2), vna::detail::VnaFrameReservationResult::Reserved);
}

TEST(VnaFrameTrackerTest, ResetAllowsThePreparedTrackerToServeAnotherSweep)
{
    vna::detail::VnaFrameTracker tracker(sparseMultiExcitationPlan(), layouts());
    ASSERT_EQ(tracker.reserve(10, 0, 2), vna::detail::VnaFrameReservationResult::Reserved);
    ASSERT_EQ(tracker.reserve(20, 0, 1), vna::detail::VnaFrameReservationResult::Reserved);
    ASSERT_EQ(tracker.reserve(20, 2, 2), vna::detail::VnaFrameReservationResult::Reserved);
    ASSERT_EQ(tracker.reserve(10, 3, 1), vna::detail::VnaFrameReservationResult::Reserved);
    ASSERT_TRUE(tracker.complete());

    tracker.resetForSweep();

    EXPECT_EQ(tracker.reservedFrameCount(), 0U);
    EXPECT_FALSE(tracker.complete());
    EXPECT_EQ(tracker.reserve(10, 0, 2), vna::detail::VnaFrameReservationResult::Reserved);
    EXPECT_EQ(tracker.reserve(20, 0, 1), vna::detail::VnaFrameReservationResult::Reserved);
}

TEST(VnaBufferPoolTest, ReportsExhaustionAsDataLoss)
{
    auto waveLayouts = layouts();
    waveLayouts.front().waveKeys.push_back(vna::VnaWaveKey{vna::WaveKind::Incident, 1});
    auto pool = vna::detail::VnaBufferPool::create(waveLayouts, 1, 1, &acceptedBlock);

    VnaBufferHandle shortHandle = nullptr;
    VnaMutableWaveBlockViewV1 shortView{};
    shortView.structSize = static_cast<std::uint32_t>(sizeof(shortView) - 1);
    EXPECT_EQ(pool->acquire(1, 10, 1, &shortHandle, &shortView).code, VNA_STATUS_INVALID_ARGUMENT);

    VnaBufferHandle reservedHandle = nullptr;
    VnaMutableWaveBlockViewV1 reservedView{};
    reservedView.structSize = static_cast<std::uint32_t>(sizeof(reservedView));
    reservedView.reserved = 1;
    EXPECT_EQ(pool->acquire(1, 10, 1, &reservedHandle, &reservedView).code, VNA_STATUS_INVALID_ARGUMENT);

    VnaBufferHandle firstHandle = nullptr;
    VnaMutableWaveBlockViewV1 firstView{};
    firstView.structSize = static_cast<std::uint32_t>(sizeof(firstView));
    ASSERT_EQ(pool->acquire(1, 10, 1, &firstHandle, &firstView).code, VNA_STATUS_OK);

    VnaBufferHandle blockedHandle = nullptr;
    VnaMutableWaveBlockViewV1 blockedView{};
    blockedView.structSize = static_cast<std::uint32_t>(sizeof(blockedView));
    EXPECT_EQ(pool->acquire(1, 10, 1, &blockedHandle, &blockedView).code, VNA_STATUS_DATA_LOSS);
    pool->discard(firstHandle);
}

TEST(VnaBufferPoolTest, RejectsUnknownQualityFlagsBeforePublishingBlock)
{
    auto waveLayouts = layouts();
    waveLayouts.front().waveKeys.push_back(vna::VnaWaveKey{vna::WaveKind::Incident, 1});
    auto pool = vna::detail::VnaBufferPool::create(waveLayouts, 1, 1, &acceptedBlock);
    VnaBufferHandle handle = nullptr;
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));

    ASSERT_EQ(pool->acquire(1, 10, 1, &handle, &view).code, VNA_STATUS_OK);
    view.qualityFlags[0] = UINT32_C(0x20);
    EXPECT_EQ(pool->commit(handle, 0, 1, 0).code, VNA_STATUS_PROTOCOL_ERROR);
    pool->discard(handle);
}

TEST(VnaBufferPoolTest, UsesTheCapacityReturnedForEachWaveStride)
{
    auto waveLayouts = layouts();
    waveLayouts.front().waveKeys = {
        vna::VnaWaveKey{vna::WaveKind::Incident, 1}, vna::VnaWaveKey{vna::WaveKind::Outgoing, 1}};
    bool observedSecondWave = false;
    auto inspectBlock = [&observedSecondWave](vna::VnaWaveBlock block) {
        observedSecondWave = block.value(1, 0) == std::complex<double>(7.0, -3.0);
        return acceptedBlock(std::move(block));
    };
    auto pool = vna::detail::VnaBufferPool::create(waveLayouts, 2, 1, inspectBlock);
    VnaBufferHandle handle = nullptr;
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));

    ASSERT_EQ(pool->acquire(1, 10, 1, &handle, &view).code, VNA_STATUS_OK);
    ASSERT_EQ(view.pointCapacity, 1U);
    view.values[1] = VnaComplex64V1{7.0, -3.0};
    ASSERT_EQ(pool->commit(handle, 0, 1, 0).code, VNA_STATUS_OK);
    EXPECT_TRUE(observedSecondWave);
}

TEST(VnaWaveBlockTest, CopiesTailBlockWithAcquireCapacityStride)
{
    auto waveLayouts = layouts();
    waveLayouts.front().waveKeys = {
        vna::VnaWaveKey{vna::WaveKind::Incident, 1}, vna::VnaWaveKey{vna::WaveKind::Outgoing, 1}};
    std::array<std::complex<double>, 2> values{};
    std::array<std::uint32_t, 2> quality{};
    auto copyBlock = [&values, &quality](vna::VnaWaveBlock block) {
        block.copyWaveTo(1, values.data(), quality.data(), values.size());
        return acceptedBlock(std::move(block));
    };
    auto pool = vna::detail::VnaBufferPool::create(waveLayouts, 3, 1, copyBlock);
    VnaBufferHandle handle = nullptr;
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));

    ASSERT_EQ(pool->acquire(1, 10, 3, &handle, &view).code, VNA_STATUS_OK);
    view.values[2] = VnaComplex64V1{99.0, 99.0};
    view.values[3] = VnaComplex64V1{7.0, -3.0};
    view.values[4] = VnaComplex64V1{-2.0, 5.0};
    view.qualityFlags[3] = VNA_QUALITY_OVERLOAD;
    view.qualityFlags[4] = VNA_QUALITY_UNLOCKED | VNA_QUALITY_REFERENCE_LOW;
    ASSERT_EQ(pool->commit(handle, 0, 2, 0).code, VNA_STATUS_OK);

    EXPECT_EQ(values, (std::array<std::complex<double>, 2>{{{7.0, -3.0}, {-2.0, 5.0}}}));
    EXPECT_EQ(
        quality,
        (std::array<std::uint32_t, 2>{{VNA_QUALITY_OVERLOAD, VNA_QUALITY_UNLOCKED | VNA_QUALITY_REFERENCE_LOW}}));
}

TEST(VnaWaveBlockTest, RejectsInvalidCopyArgumentsBeforeWriting)
{
    std::complex<double> value{13.0, -9.0};
    std::uint32_t quality = 42;
    auto checkBlock = [&value, &quality](vna::VnaWaveBlock block) {
        EXPECT_THROW(block.copyWaveTo(0, nullptr, &quality, 0), std::invalid_argument);
        EXPECT_THROW(block.copyWaveTo(0, &value, nullptr, 0), std::invalid_argument);
        EXPECT_THROW(block.copyWaveTo(1, &value, &quality, 0), std::out_of_range);
        EXPECT_THROW(block.copyWaveTo(0, &value, &quality, 2), std::out_of_range);
        EXPECT_NO_THROW(block.copyWaveTo(0, &value, &quality, 0));
        return acceptedBlock(std::move(block));
    };
    auto waveLayouts = layouts();
    waveLayouts.front().waveKeys.push_back(vna::VnaWaveKey{vna::WaveKind::Incident, 1});
    auto pool = vna::detail::VnaBufferPool::create(waveLayouts, 1, 1, checkBlock);
    VnaBufferHandle handle = nullptr;
    VnaMutableWaveBlockViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));

    ASSERT_EQ(pool->acquire(1, 10, 1, &handle, &view).code, VNA_STATUS_OK);
    ASSERT_EQ(pool->commit(handle, 0, 1, 0).code, VNA_STATUS_OK);
    EXPECT_EQ(value, std::complex<double>(13.0, -9.0));
    EXPECT_EQ(quality, 42U);

    vna::VnaWaveBlock empty;
    EXPECT_THROW(empty.copyWaveTo(0, &value, &quality, 0), std::logic_error);
}

}  // namespace
