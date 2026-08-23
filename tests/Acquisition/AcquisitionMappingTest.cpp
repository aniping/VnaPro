#include "AcquisitionTestSupport.h"

namespace vna::test {
namespace {

    ::testing::AssertionResult seriesMatches(
        const RawSweepFrame &frame, ExcitationId excitationId, VnaWaveKey wave,
        const std::vector<std::uint32_t> &pointIndices, SweepId sweepId)
    {
        const auto series = frame.findWave(excitationId, wave);
        if (!series) {
            return ::testing::AssertionFailure() << "wave series was not found";
        }
        if (series->size != pointIndices.size()) {
            return ::testing::AssertionFailure() << "unexpected series size " << series->size;
        }
        const auto &points = frame.prepared().hardwarePrepared().effectivePlan.points;
        for (std::size_t index = 0; index < pointIndices.size(); ++index) {
            const std::uint32_t pointIndex = pointIndices[index];
            const WaveSample expected = expectedSample(excitationId, wave, points[pointIndex].pointId, sweepId);
            if ((series->pointIndices[index] != pointIndex) || (series->values[index] != expected.value) ||
                (series->quality[index] != expected.quality)) {
                return ::testing::AssertionFailure()
                       << "series mismatch at local index " << index << ", point index " << pointIndex;
            }
        }
        return ::testing::AssertionSuccess();
    }

}  // namespace

TEST_F(AcquisitionTest, AssemblesSparseLayoutsWithoutTreatingIdentifiersAsIndices)
{
    ASSERT_NO_THROW(prepareSuccessfully(91));
    const auto run = engine_->start(AcquisitionRunRequest{AcquisitionRunMode::Finite, 1});
    ASSERT_TRUE(run.hasValue());
    const SweepId sweepId = 903;

    const auto blocks = makeSparseBlocks(plan_, sweepId);
    ASSERT_EQ(blocks.size(), 4U);
    EXPECT_EQ(blocks[1].firstPointIndex, 2U);
    EXPECT_EQ(blocks[1].waves.front().size(), 1U);
    ASSERT_TRUE(completeCurrentSweep(sweepId));
    ASSERT_TRUE(sink_.waitForFrames(1));
    ASSERT_TRUE(sink_.waitForCompletions(1));

    const auto frame = sink_.frame(0);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->runId(), run.value());
    EXPECT_EQ(frame->sweepIndex(), 0U);
    EXPECT_EQ(frame->hardwareSweepId(), sweepId);
    EXPECT_EQ(frame->prepared().pointExcitationRecordCount(), 7U);
    EXPECT_EQ(frame->prepared().waveSampleCountPerSweep(), 14U);
    EXPECT_EQ(frame->prepared().hardwarePrepared().effectivePlan.points[0].pointId, 503U);
    EXPECT_TRUE(seriesMatches(*frame, 40, VnaWaveKey{WaveKind::Outgoing, 3}, {0, 2, 4}, sweepId));
    EXPECT_TRUE(seriesMatches(*frame, 40, VnaWaveKey{WaveKind::Incident, 1}, {0, 2, 4}, sweepId));
    EXPECT_TRUE(seriesMatches(*frame, 10, VnaWaveKey{WaveKind::Incident, 1}, {0, 1, 2, 3}, sweepId));
    EXPECT_TRUE(seriesMatches(*frame, 10, VnaWaveKey{WaveKind::Outgoing, 3}, {0, 1, 2, 3}, sweepId));
    EXPECT_FALSE(frame->findWave(99, VnaWaveKey{WaveKind::Incident, 1}));
    EXPECT_FALSE(frame->findWave(10, VnaWaveKey{WaveKind::Outgoing, 4}));

    const auto completion = sink_.completion(0);
    EXPECT_EQ(completion.runId, run.value());
    EXPECT_EQ(completion.termination, AcquisitionRunTermination::Completed);
    EXPECT_EQ(completion.completedSweepCount, 1U);
    EXPECT_FALSE(completion.error.has_value());
}

}  // namespace vna::test
