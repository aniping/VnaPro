#include "SimulatorDevice.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct CommitRecord {
    std::uint32_t layoutId{0};
    std::uint32_t firstPoint{0};
    std::uint32_t pointCount{0};
    bool payloadComplete{false};
};

struct PointRecord {
    std::uint32_t layoutId{0};
    std::uint32_t pointIndex{0};
};

class SparseLayoutHost final {
public:
    SparseLayoutHost()
    {
        block_.owner = this;
        active_ = this;
    }

    ~SparseLayoutHost()
    {
        active_ = nullptr;
    }
    SparseLayoutHost(const SparseLayoutHost &) = delete;
    SparseLayoutHost &operator=(const SparseLayoutHost &) = delete;
    SparseLayoutHost(SparseLayoutHost &&) = delete;
    SparseLayoutHost &operator=(SparseLayoutHost &&) = delete;

    VnaDriverHostApiV1 api() const
    {
        VnaDriverHostApiV1 result{};
        result.structSize = static_cast<std::uint32_t>(sizeof(result));
        result.abiMajor = VNA_DRIVER_ABI_MAJOR_V1;
        result.abiMinor = VNA_DRIVER_ABI_MINOR_V1;
        result.Log = &log;
        result.IsCancellationRequested = &isCancellationRequested;
        result.AcquireWaveBlock = &acquireWaveBlock;
        result.CommitWaveBlock = &commitWaveBlock;
        result.DiscardWaveBlock = &discardWaveBlock;
        return result;
    }

    std::size_t recordCount() const
    {
        return recordCount_;
    }
    const CommitRecord &record(std::size_t index) const
    {
        return records_[index];
    }
    std::uint32_t discardCount() const
    {
        return discardCount_;
    }

private:
    struct WaveBlock {
        SparseLayoutHost *owner{nullptr};
        std::uint32_t layoutId{0};
        std::uint32_t capacity{0};
        std::array<VnaComplex64V1, vna::simulator::simulatorBatchPoints> values{};
        std::array<std::uint32_t, vna::simulator::simulatorBatchPoints> quality{};
    };

    static VnaStatusV1 status(std::uint32_t code)
    {
        VnaStatusV1 result{};
        result.code = code;
        return result;
    }

    static void log(std::uint32_t, VnaStringViewV1)
    {
    }
    static std::uint8_t isCancellationRequested(VnaSweepIdV1)
    {
        return 0;
    }

    static VnaStatusV1 acquireWaveBlock(
        VnaSweepIdV1, std::uint32_t layoutId, std::uint32_t capacity, VnaBufferHandle *handle,
        VnaMutableWaveBlockViewV1 *view)
    {
        if ((active_ == nullptr) || (capacity == 0) || (capacity > active_->block_.values.size())) {
            return status(VNA_STATUS_INVALID_ARGUMENT);
        }
        auto &block = active_->block_;
        block.layoutId = layoutId;
        block.capacity = capacity;
        block.values.fill(
            VnaComplex64V1{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()});
        block.quality.fill(std::numeric_limits<std::uint32_t>::max());
        *handle = &block;
        *view = VnaMutableWaveBlockViewV1{
            static_cast<std::uint32_t>(sizeof(*view)), capacity, 1, 0, block.values.data(), block.quality.data()};
        return status(VNA_STATUS_OK);
    }

    static bool payloadComplete(const WaveBlock &block, std::uint32_t pointCount)
    {
        for (std::uint32_t index = 0; index < pointCount; ++index) {
            const auto &value = block.values[index];
            if (!std::isfinite(value.real) || !std::isfinite(value.imag) ||
                (block.quality[index] != VNA_QUALITY_VALID)) {
                return false;
            }
        }
        return true;
    }

    static VnaStatusV1 commitWaveBlock(VnaBufferHandle handle, std::uint32_t firstPoint, std::uint32_t pointCount)
    {
        auto *block = static_cast<WaveBlock *>(handle);
        if ((block == nullptr) || (block->owner == nullptr) || (pointCount == 0) || (pointCount > block->capacity)) {
            return status(VNA_STATUS_INVALID_ARGUMENT);
        }
        auto &host = *block->owner;
        if (host.recordCount_ >= host.records_.size()) {
            return status(VNA_STATUS_DATA_LOSS);
        }
        host.records_[host.recordCount_++] =
            CommitRecord{block->layoutId, firstPoint, pointCount, payloadComplete(*block, pointCount)};
        return status(VNA_STATUS_OK);
    }

    static void discardWaveBlock(VnaBufferHandle handle)
    {
        auto *block = static_cast<WaveBlock *>(handle);
        if ((block != nullptr) && (block->owner != nullptr)) {
            ++block->owner->discardCount_;
        }
    }

    inline static SparseLayoutHost *active_{nullptr};
    WaveBlock block_{};
    std::array<CommitRecord, 4> records_{};
    std::size_t recordCount_{0};
    std::uint32_t discardCount_{0};
};

VnaExcitationPlanViewV1 makeExcitation(
    std::uint16_t identifier, const VnaSourcePlanV1 *source, const VnaWaveKeyV1 *wave)
{
    return VnaExcitationPlanViewV1{identifier, 0, 1, source, 1, wave};
}

VnaPointPlanViewV1 makePoint(
    std::uint32_t identifier, double frequencyHz, const VnaExcitationPlanViewV1 *excitations,
    std::size_t excitationCount)
{
    return VnaPointPlanViewV1{identifier, 0, frequencyHz, 10.0e6, 0, excitationCount, excitations};
}

struct SparsePlan {
    SparsePlan()
    {
        sources = {VnaSourcePlanV1{1, 0, -10.0, 0.0}, VnaSourcePlanV1{2, 0, -10.0, 90.0}};
        waves = {VnaWaveKeyV1{VNA_WAVE_OUTGOING, 1, 0}, VnaWaveKeyV1{VNA_WAVE_OUTGOING, 2, 0}};
        const auto first = makeExcitation(11, &sources[0], &waves[0]);
        const auto second = makeExcitation(22, &sources[1], &waves[1]);
        both = {first, second};
        firstOnly = {first};
        secondOnly = {second};
        points = {
            makePoint(101, 1.0e9, both.data(), both.size()), makePoint(102, 1.1e9, firstOnly.data(), firstOnly.size()),
            makePoint(103, 1.2e9, secondOnly.data(), secondOnly.size()),
            makePoint(104, 1.3e9, both.data(), both.size())};
        plan = VnaSweepPlanViewV1{
            static_cast<std::uint32_t>(sizeof(plan)), VNA_TRIGGER_INTERNAL, 0, 0, points.size(), points.data()};
        result.structSize = static_cast<std::uint32_t>(sizeof(result));
    }

    std::array<VnaSourcePlanV1, 2> sources{};
    std::array<VnaWaveKeyV1, 2> waves{};
    std::array<VnaExcitationPlanViewV1, 2> both{};
    std::array<VnaExcitationPlanViewV1, 1> firstOnly{};
    std::array<VnaExcitationPlanViewV1, 1> secondOnly{};
    std::array<VnaPointPlanViewV1, 4> points{};
    VnaSweepPlanViewV1 plan{};
    VnaPrepareResultViewV1 result{};
};

void expectCommitSequence(const SparseLayoutHost &host)
{
    const std::array<CommitRecord, 4> expected{
        CommitRecord{1, 0, 2, true}, CommitRecord{1, 3, 1, true}, CommitRecord{2, 0, 1, true},
        CommitRecord{2, 2, 2, true}};
    ASSERT_EQ(host.recordCount(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(host.record(index).layoutId, expected[index].layoutId);
        EXPECT_EQ(host.record(index).firstPoint, expected[index].firstPoint);
        EXPECT_EQ(host.record(index).pointCount, expected[index].pointCount);
        EXPECT_EQ(host.record(index).payloadComplete, expected[index].payloadComplete);
    }
}

void expectPointCoverage(const SparseLayoutHost &host)
{
    std::vector<PointRecord> actual;
    for (std::size_t index = 0; index < host.recordCount(); ++index) {
        const auto &commit = host.record(index);
        for (std::uint32_t offset = 0; offset < commit.pointCount; ++offset) {
            actual.push_back(PointRecord{commit.layoutId, commit.firstPoint + offset});
        }
    }
    const std::array<PointRecord, 6> expected{PointRecord{1, 0}, PointRecord{1, 1}, PointRecord{1, 3},
                                              PointRecord{2, 0}, PointRecord{2, 2}, PointRecord{2, 3}};
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index].layoutId, expected[index].layoutId);
        EXPECT_EQ(actual[index].pointIndex, expected[index].pointIndex);
    }
}

TEST(SimulatorSparseLayoutTest, CommitsLayoutMajorBatchesAcrossSparsePointHoles)
{
    SparseLayoutHost host;
    vna::simulator::SimulatorDevice device(host.api());
    SparsePlan storage;

    ASSERT_EQ(device.connect().code, VNA_STATUS_OK);
    ASSERT_EQ(device.prepare(&storage.plan, &storage.result).code, VNA_STATUS_OK);
    ASSERT_EQ(storage.result.layoutCount, 2U);
    EXPECT_EQ(storage.result.layouts[0].excitationId, 11U);
    EXPECT_EQ(storage.result.layouts[1].excitationId, 22U);
    EXPECT_EQ(device.execute(1).code, VNA_STATUS_OK);

    // 精确序列同时证明布局主序，以及两个布局遇到各自孔洞后拆块并继续扫描。
    expectCommitSequence(host);
    expectPointCoverage(host);
    EXPECT_EQ(host.discardCount(), 0U);
    EXPECT_EQ(device.disconnect().code, VNA_STATUS_OK);
}

}  // namespace
