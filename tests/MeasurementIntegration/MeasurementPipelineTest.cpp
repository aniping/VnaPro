#include "AcquisitionTestSupport.h"
#include "MeasurementTestSupport.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace vna::test {
namespace {

    WaveSample pipelineSample(ExcitationId excitationId, VnaWaveKey wave, std::uint32_t pointIndex)
    {
        const double identity = static_cast<double>(excitationId) * 10.0 + static_cast<double>(pointIndex) + 1.0;
        const double direction = wave.kind == WaveKind::Incident ? 1.0 : 2.0 + static_cast<double>(wave.port);
        return WaveSample{{identity * direction, identity * 0.25 - direction}, 0};
    }

    std::vector<std::uint32_t> pointIndicesFor(const VnaSweepPlan &plan, ExcitationId excitationId)
    {
        std::vector<std::uint32_t> indices;
        for (std::size_t pointIndex = 0; pointIndex < plan.points.size(); ++pointIndex) {
            for (const auto &excitation : plan.points[pointIndex].excitations) {
                if (excitation.excitationId == excitationId) {
                    indices.push_back(static_cast<std::uint32_t>(pointIndex));
                }
            }
        }
        return indices;
    }

    WaveBlockSpec makeBlock(const VnaWaveLayout &layout, const std::vector<std::uint32_t> &indices, SweepId sweepId)
    {
        WaveBlockSpec block;
        block.sweepId = sweepId;
        block.layoutId = layout.layoutId;
        block.firstPointIndex = indices.front();
        block.waves.resize(layout.waveKeys.size());
        for (std::size_t wave = 0; wave < layout.waveKeys.size(); ++wave) {
            for (const auto pointIndex : indices) {
                block.waves[wave].push_back(pipelineSample(layout.excitationId, layout.waveKeys[wave], pointIndex));
            }
        }
        return block;
    }

    void appendLayoutBlocks(
        const VnaSweepPlan &plan, const VnaWaveLayout &layout, SweepId sweepId, std::vector<WaveBlockSpec> &blocks)
    {
        const auto indices = pointIndicesFor(plan, layout.excitationId);
        std::size_t first = 0;
        while (first < indices.size()) {
            std::size_t last = first + 1;
            while ((last < indices.size()) && (indices[last] == indices[last - 1] + 1U)) {
                ++last;
            }
            const std::vector<std::uint32_t> range(indices.begin() + first, indices.begin() + last);
            blocks.push_back(makeBlock(layout, range, sweepId));
            first = last;
        }
    }

    std::vector<WaveBlockSpec> makeBlocks(const PreparedSweep &prepared, SweepId sweepId)
    {
        std::vector<WaveBlockSpec> blocks;
        for (const auto &layout : prepared.layouts) {
            appendLayoutBlocks(prepared.effectivePlan, layout, sweepId, blocks);
        }
        for (std::size_t sequence = 0; sequence < blocks.size(); ++sequence) {
            blocks[sequence].sequence = sequence;
        }
        return blocks;
    }

    std::size_t excitationOccurrenceCount(const VnaSweepPlan &plan)
    {
        std::size_t count = 0;
        for (const auto &point : plan.points) {
            count += point.excitations.size();
        }
        return count;
    }

}  // namespace

class MeasurementPipelineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        hardware_ = std::make_shared<FakeVnaHardware>(false);
        auto created = createAcquisitionEngine(hardware_, sink_, spaciousAcquisitionLimits());
        ASSERT_TRUE(created.hasValue()) << created.error().description;
        engine_ = created.takeValue();
    }

    void compileAndPrepare()
    {
        auto compiled = compileMeasurement(makeSparseMeasurementRequest(), spaciousMeasurementLimits());
        ASSERT_TRUE(compiled.hasValue()) << compiled.error().description;
        auto compilation = compiled.takeValue();
        compiled_ = compilation.compiled;
        preparedSweep_ = makeMeasurementPreparedSweep(compilation.sweepPlan, 801);
        auto submitted = engine_->prepareAsync(std::move(compilation.sweepPlan));
        ASSERT_TRUE(submitted.hasValue()) << submitted.error().description;
        hardware_->publishPrepareSucceeded(submitted.value(), preparedSweep_);
        ASSERT_TRUE(sink_.waitForPrepared(1));
        const auto notice = sink_.prepared(0);
        ASSERT_TRUE(notice.prepared) << notice.error.description;
        auto bound = bindMeasurement(compiled_, notice.prepared);
        ASSERT_TRUE(bound.hasValue()) << bound.error().description;
        prepared_ = bound.takeValue();
    }

    void runAndBuild()
    {
        sink_.setCompletionEntryHook([this] { completionObservedFrame_.store(sink_.frameCount() == 1U); });
        auto started = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
        ASSERT_TRUE(started.hasValue()) << started.error().description;
        runId_ = started.value();
        ASSERT_TRUE(hardware_->waitForStartCalls(1));
        hardware_->publishStartSucceeded(hardware_->lastStartCommand(), sweepId_);
        for (const auto &block : makeBlocks(preparedSweep_, sweepId_)) {
            ASSERT_TRUE(hardware_->publishWaveBlock(block));
        }
        const std::size_t recordCount = excitationOccurrenceCount(preparedSweep_.effectivePlan);
        hardware_->publishSweepFinished(
            SweepCompletion{sweepId_, SweepTerminationReason::Completed, recordCount, recordCount, 0},
            DeviceState::Prepared);
        ASSERT_TRUE(sink_.waitForFrames(1));
        ASSERT_TRUE(sink_.waitForCompletions(1));
        raw_ = sink_.frame(0);
        auto built = buildObservationFrame(prepared_, *raw_);
        ASSERT_TRUE(built.hasValue()) << built.error().description;
        observation_ = built.takeValue();
    }

    void expectRatio(ObservationId id, VnaWaveKey numeratorKey, VnaWaveKey denominatorKey)
    {
        const auto numerator = raw_->findWave(10, numeratorKey);
        const auto denominator = raw_->findWave(10, denominatorKey);
        const auto observed = observation_->findObservation(id);
        ASSERT_TRUE(numerator);
        ASSERT_TRUE(denominator);
        ASSERT_TRUE(observed);
        ASSERT_EQ(observed->size, numerator->size);
        ASSERT_EQ(observed->size, denominator->size);
        for (std::size_t index = 0; index < observed->size; ++index) {
            EXPECT_EQ(observed->pointIndices[index], numerator->pointIndices[index]);
            EXPECT_EQ(observed->values[index], numerator->values[index] / denominator->values[index]);
            EXPECT_EQ(observed->status[index].numeratorWaveQuality, numerator->quality[index]);
            EXPECT_EQ(observed->status[index].denominatorWaveQuality, denominator->quality[index]);
            EXPECT_EQ(observed->status[index].calculationQuality, 0U);
        }
    }

    void expectTerminalContract()
    {
        ASSERT_EQ(sink_.frameCount(), 1U);
        ASSERT_EQ(sink_.completionCount(), 1U);
        EXPECT_TRUE(completionObservedFrame_.load());
        const auto completion = sink_.completion(0);
        EXPECT_EQ(completion.runId, runId_);
        EXPECT_EQ(completion.termination, AcquisitionRunTermination::Completed);
        EXPECT_EQ(completion.completedSweepCount, 1U);
        EXPECT_EQ(observation_->runId(), runId_);
        EXPECT_EQ(observation_->hardwareSweepId(), sweepId_);
    }

    RecordingAcquisitionSink sink_;
    std::shared_ptr<FakeVnaHardware> hardware_;
    std::shared_ptr<const CompiledMeasurement> compiled_;
    std::shared_ptr<const PreparedMeasurement> prepared_;
    std::shared_ptr<const RawSweepFrame> raw_;
    std::shared_ptr<const ObservationFrame> observation_;
    PreparedSweep preparedSweep_;
    AcquisitionRunId runId_{0};
    SweepId sweepId_{9001};
    std::atomic<bool> completionObservedFrame_{false};
    // 最后声明以保证最先析构；同步退订完成后，其他回调捕获状态才开始析构。
    std::unique_ptr<IVnaAcquisitionEngine> engine_;
};

TEST_F(MeasurementPipelineTest, ConvertsOneAcquisitionFrameBeforeTheUniqueRunTerminal)
{
    compileAndPrepare();
    runAndBuild();
    expectRatio(101, outgoingWave(1), incidentWave(1));
    expectRatio(102, outgoingWave(2), incidentWave(1));
    expectTerminalContract();
}

}  // namespace vna::test
