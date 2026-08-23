#include "AcquisitionTestSupport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vna::test {
namespace {

    constexpr std::size_t largePointCount = 10'000;
    constexpr std::size_t largeExcitationCount = 4;
    constexpr std::size_t largeWaveCount = 4;
    constexpr std::size_t largeRecordCount = largePointCount * largeExcitationCount;
    constexpr std::size_t largeSampleCount = largeRecordCount * largeWaveCount;
    constexpr std::size_t expectedRawFrameBudgetBytes = 3'200'032;

    struct LayoutDefinition {
        ExcitationId excitationId{0};
        LayoutId layoutId{0};
        PortId sourcePort{0};
        std::array<PortId, largeWaveCount> outgoingPorts{};
    };

    constexpr std::array<LayoutDefinition, largeExcitationCount> layoutDefinitions{
        LayoutDefinition{40, 901, 3, {3, 1, 4, 2}}, LayoutDefinition{10, 17, 1, {2, 4, 1, 3}},
        LayoutDefinition{70, 503, 4, {4, 2, 3, 1}}, LayoutDefinition{25, 211, 2, {1, 3, 2, 4}}};
    constexpr std::array<std::size_t, 3> probePointIndices{0, largePointCount / 2, largePointCount - 1};

    const LayoutDefinition &definitionFor(ExcitationId excitationId)
    {
        for (const auto &definition : layoutDefinitions) {
            if (definition.excitationId == excitationId) {
                return definition;
            }
        }
        throw std::logic_error("large-shape excitation is unknown");
    }

    PointId largePointId(std::size_t pointIndex)
    {
        // 7919 与 10000 互质，得到确定且不连续的全排列；偏移保证 ID 不会等于数组下标。
        const std::size_t permuted = (pointIndex * 7919U) % largePointCount;
        return static_cast<PointId>(500'001U + (permuted * 3U));
    }

    std::array<ExcitationId, largeExcitationCount> excitationOrder(std::size_t pointIndex)
    {
        switch (pointIndex % 3U) {
            case 0:
                return {40, 10, 70, 25};
            case 1:
                return {25, 70, 10, 40};
            default:
                return {10, 25, 40, 70};
        }
    }

    std::vector<VnaWaveKey> wavesFor(const LayoutDefinition &definition)
    {
        std::vector<VnaWaveKey> waves;
        waves.reserve(largeWaveCount);
        for (const PortId port : definition.outgoingPorts) {
            waves.push_back(VnaWaveKey{WaveKind::Outgoing, port});
        }
        return waves;
    }

    VnaExcitationPlan makeExcitation(ExcitationId excitationId)
    {
        const auto &definition = definitionFor(excitationId);
        VnaExcitationPlan excitation;
        excitation.excitationId = excitationId;
        excitation.sources.push_back(VnaSourcePlan{definition.sourcePort, -10.0, 0.0});
        excitation.requiredWaves = wavesFor(definition);
        return excitation;
    }

    VnaSweepPlan makeLargePlan()
    {
        VnaSweepPlan plan;
        plan.points.reserve(largePointCount);
        for (std::size_t pointIndex = 0; pointIndex < largePointCount; ++pointIndex) {
            VnaPointPlan point;
            point.pointId = largePointId(pointIndex);
            point.frequencyHz = 1.0e6 + (static_cast<double>(pointIndex) * 1.0e6);
            point.ifBandwidthHz = 30'000.0;
            point.excitations.reserve(largeExcitationCount);
            for (const ExcitationId excitationId : excitationOrder(pointIndex)) {
                point.excitations.push_back(makeExcitation(excitationId));
            }
            plan.points.push_back(std::move(point));
        }
        return plan;
    }

    PreparedSweep makeLargePrepared(VnaSweepPlan plan, ConfigurationId configurationId)
    {
        PreparedSweep prepared;
        prepared.configurationId = configurationId;
        prepared.effectivePlan = std::move(plan);
        // Prepare 返回顺序、LayoutId 和 ExcitationId 均刻意不形成自然索引关系。
        constexpr std::array<ExcitationId, largeExcitationCount> preparedOrder{70, 40, 25, 10};
        for (const ExcitationId excitationId : preparedOrder) {
            const auto &definition = definitionFor(excitationId);
            prepared.layouts.push_back(
                VnaWaveLayout{definition.layoutId, definition.excitationId, wavesFor(definition)});
        }
        prepared.report.waveBlockPointCapacity = largePointCount;
        prepared.report.physicalSweepCount = 1;
        prepared.report.atomic = true;
        prepared.report.phaseCoherent = true;
        return prepared;
    }

    WaveBlockSpec makeLargeBlock(const LayoutDefinition &definition, SweepId sweepId, std::uint64_t sequence)
    {
        WaveBlockSpec block;
        block.sweepId = sweepId;
        block.sequence = sequence;
        block.layoutId = definition.layoutId;
        block.firstPointIndex = 0;
        const auto waves = wavesFor(definition);
        block.waves.resize(waves.size());
        for (std::size_t waveIndex = 0; waveIndex < waves.size(); ++waveIndex) {
            auto &samples = block.waves[waveIndex];
            samples.reserve(largePointCount);
            for (std::size_t pointIndex = 0; pointIndex < largePointCount; ++pointIndex) {
                samples.push_back(
                    expectedSample(definition.excitationId, waves[waveIndex], largePointId(pointIndex), sweepId));
            }
        }
        return block;
    }

    bool completeLargeSweep(FakeVnaHardware &hardware, CommandId commandId, SweepId sweepId)
    {
        hardware.publishStartSucceeded(commandId, sweepId);
        constexpr std::array<ExcitationId, largeExcitationCount> deliveryOrder{25, 10, 40, 70};
        for (std::size_t sequence = 0; sequence < deliveryOrder.size(); ++sequence) {
            const auto block = makeLargeBlock(definitionFor(deliveryOrder[sequence]), sweepId, sequence);
            if (!hardware.publishWaveBlock(block)) {
                return false;
            }
        }
        hardware.publishSweepFinished(
            SweepCompletion{sweepId, SweepTerminationReason::Completed, largeRecordCount, largeRecordCount, 0},
            DeviceState::Prepared);
        return true;
    }

    bool sampleMatches(const RawWaveSeriesView &series, std::size_t pointIndex, const WaveSample &expected)
    {
        return (series.pointIndices[pointIndex] == pointIndex) && (series.values[pointIndex] == expected.value) &&
               (series.quality[pointIndex] == expected.quality);
    }

    ::testing::AssertionResult largeSeriesMatches(
        const RawSweepFrame &frame, const LayoutDefinition &definition, VnaWaveKey wave, SweepId sweepId)
    {
        const auto series = frame.findWave(definition.excitationId, wave);
        if (!series) {
            return ::testing::AssertionFailure()
                   << "missing series for excitation " << definition.excitationId << " port " << wave.port;
        }
        if (series->size != largePointCount) {
            return ::testing::AssertionFailure() << "unexpected series size " << series->size;
        }
        for (const std::size_t pointIndex : probePointIndices) {
            const WaveSample expected =
                expectedSample(definition.excitationId, wave, largePointId(pointIndex), sweepId);
            if (!sampleMatches(*series, pointIndex, expected)) {
                return ::testing::AssertionFailure() << "series mismatch at point " << pointIndex << " for excitation "
                                                     << definition.excitationId << " port " << wave.port;
            }
        }
        return ::testing::AssertionSuccess();
    }

    void expectLargePreparedShape(const PreparedAcquisition &prepared)
    {
        EXPECT_EQ(prepared.pointExcitationRecordCount(), largeRecordCount);
        EXPECT_EQ(prepared.waveSampleCountPerSweep(), largeSampleCount);
        EXPECT_EQ(prepared.hardwarePrepared().report.waveBlockPointCapacity, largePointCount);

        const auto &effectivePoints = prepared.hardwarePrepared().effectivePlan.points;
        ASSERT_EQ(effectivePoints.size(), largePointCount);
        for (const std::size_t pointIndex : probePointIndices) {
            EXPECT_EQ(effectivePoints[pointIndex].pointId, largePointId(pointIndex));
            EXPECT_NE(effectivePoints[pointIndex].pointId, pointIndex);
            const auto expectedOrder = excitationOrder(pointIndex);
            ASSERT_EQ(effectivePoints[pointIndex].excitations.size(), expectedOrder.size());
            for (std::size_t excitationIndex = 0; excitationIndex < expectedOrder.size(); ++excitationIndex) {
                EXPECT_EQ(
                    effectivePoints[pointIndex].excitations[excitationIndex].excitationId,
                    expectedOrder[excitationIndex]);
            }
        }
    }

    void expectLargeFrameShape(const RawSweepFrame &frame, AcquisitionRunId runId, SweepId sweepId)
    {
        EXPECT_EQ(frame.runId(), runId);
        EXPECT_EQ(frame.hardwareSweepId(), sweepId);
        EXPECT_EQ(frame.prepared().pointExcitationRecordCount(), largeRecordCount);
        EXPECT_EQ(frame.prepared().waveSampleCountPerSweep(), largeSampleCount);
        for (const auto &definition : layoutDefinitions) {
            for (const PortId port : definition.outgoingPorts) {
                EXPECT_TRUE(largeSeriesMatches(frame, definition, VnaWaveKey{WaveKind::Outgoing, port}, sweepId));
            }
        }
    }

}  // namespace

TEST_F(AcquisitionTest, FourPortLargeShapeAssemblesAllSeriesWithExactBudget)
{
    plan_ = makeLargePlan();
    const AcquisitionLimits exact = exactLimitsFor(plan_);
    ASSERT_EQ(sizeof(std::size_t), 8U);
    ASSERT_EQ(exact.maximumRawFramePayloadBytes, expectedRawFrameBudgetBytes);
    ASSERT_NO_THROW(resetEngine(true, exact));

    const auto submitted = engine_->prepareAsync(plan_);
    ASSERT_TRUE(submitted.hasValue()) << submitted.error().description;
    prepared_ = makeLargePrepared(std::move(plan_), 1701);
    hardware_->publishPrepareSucceeded(submitted.value(), std::move(prepared_));
    ASSERT_TRUE(sink_.waitForPrepared(1));
    const auto prepared = sink_.prepared(0).prepared;
    ASSERT_NE(prepared, nullptr);
    expectLargePreparedShape(*prepared);

    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue()) << run.error().description;
    const SweepId sweepId = 17001;
    ASSERT_TRUE(completeLargeSweep(*hardware_, hardware_->lastStartCommand(), sweepId));
    ASSERT_TRUE(sink_.waitForFrames(1));
    ASSERT_TRUE(sink_.waitForCompletions(1));

    const auto frame = sink_.frame(0);
    ASSERT_NE(frame, nullptr);
    expectLargeFrameShape(*frame, run.value(), sweepId);

    EXPECT_EQ(sink_.frameCount(), 1U);
    EXPECT_EQ(sink_.completionCount(), 1U);
    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.runId, run.value());
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Completed);
    EXPECT_EQ(completion.completedSweepCount, 1U);
    EXPECT_FALSE(completion.error.has_value());
    EXPECT_EQ(engine_->state(), AcquisitionState::Ready);
}

TEST_F(AcquisitionTest, FourPortLargeShapeRejectsRawBudgetOneByteShortBeforeHardware)
{
    plan_ = makeLargePlan();
    AcquisitionLimits limits = exactLimitsFor(plan_);
    ASSERT_EQ(limits.maximumRawFramePayloadBytes, expectedRawFrameBudgetBytes);
    --limits.maximumRawFramePayloadBytes;
    ASSERT_NO_THROW(resetEngine(true, limits));

    const auto submitted = engine_->prepareAsync(std::move(plan_));

    ASSERT_FALSE(submitted.hasValue());
    EXPECT_EQ(submitted.error().code, VnaErrorCode::UnsupportedPlan);
    EXPECT_EQ(hardware_->validationCallCount(), 0U);
    EXPECT_EQ(hardware_->prepareCallCount(), 0U);
    EXPECT_EQ(sink_.preparedCount(), 0U);
    EXPECT_EQ(engine_->state(), AcquisitionState::Unprepared);
}

}  // namespace vna::test
