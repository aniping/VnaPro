#include "SimulatorTiming.h"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

vna::simulator::Point makePoint(
    double ifBandwidthHz, std::int64_t dwellNanoseconds, std::initializer_list<std::uint32_t> layoutIds)
{
    vna::simulator::Point point;
    point.ifBandwidthHz = ifBandwidthHz;
    point.dwellNanoseconds = dwellNanoseconds;
    for (const std::uint32_t layoutId : layoutIds) {
        vna::simulator::Excitation excitation;
        excitation.identifier = static_cast<std::uint16_t>(layoutId);
        excitation.layoutId = layoutId;
        excitation.sources.push_back(vna::simulator::Source{1, 0.0, 0.0});
        excitation.waves.push_back(vna::simulator::Wave{VNA_WAVE_INCIDENT, 1});
        point.excitations.push_back(std::move(excitation));
    }
    return point;
}

TEST(SimulatorTimingTest, ConvertsTenToThirtyKilohertzAndRoundsUp)
{
    const std::vector<vna::simulator::Point> points{makePoint(10000.0, 0, {1}), makePoint(30000.0, 0, {1})};

    const auto timing = vna::simulator::calculateBatchMeasurementTiming(points, 0, 2, 1);

    ASSERT_TRUE(timing.valid);
    EXPECT_EQ(timing.duration, std::chrono::nanoseconds(133334));
}

TEST(SimulatorTimingTest, CountsEveryExcitationButPointDwellOnlyOnce)
{
    const std::vector<vna::simulator::Point> points{makePoint(1000.0, 250000, {2, 1})};

    const auto firstLayout = vna::simulator::calculateBatchMeasurementTiming(points, 0, 1, 1);
    const auto secondLayout = vna::simulator::calculateBatchMeasurementTiming(points, 0, 1, 2);

    ASSERT_TRUE(firstLayout.valid);
    ASSERT_TRUE(secondLayout.valid);
    EXPECT_EQ(firstLayout.duration, std::chrono::microseconds(1250));
    EXPECT_EQ(secondLayout.duration, std::chrono::milliseconds(1));
}

TEST(SimulatorTimingTest, SourceAndWaveCountsDoNotMultiplyMeasurementTime)
{
    auto minimal = makePoint(10000.0, 0, {1});
    auto parallelChannels = minimal;
    auto &excitation = parallelChannels.excitations.front();
    excitation.sources.clear();
    excitation.waves.clear();
    for (std::uint16_t port = 1; port <= 4; ++port) {
        excitation.sources.push_back(vna::simulator::Source{port, 0.0, 0.0});
        excitation.waves.push_back(vna::simulator::Wave{VNA_WAVE_INCIDENT, port});
        excitation.waves.push_back(vna::simulator::Wave{VNA_WAVE_OUTGOING, port});
    }
    const std::vector<vna::simulator::Point> minimalPoints{std::move(minimal)};
    const std::vector<vna::simulator::Point> parallelPoints{std::move(parallelChannels)};

    const auto minimalTiming = vna::simulator::calculateBatchMeasurementTiming(minimalPoints, 0, 1, 1);
    const auto parallelTiming = vna::simulator::calculateBatchMeasurementTiming(parallelPoints, 0, 1, 1);

    ASSERT_TRUE(minimalTiming.valid);
    ASSERT_TRUE(parallelTiming.valid);
    EXPECT_EQ(minimalTiming.duration, parallelTiming.duration);
}

TEST(SimulatorTimingTest, ChargesOnlyTheActualTailAndRejectsLayoutHoles)
{
    const std::vector<vna::simulator::Point> points{
        makePoint(1000.0, 0, {1}), makePoint(1000.0, 0, {1}), makePoint(2000.0, 0, {2})};

    const auto fullBatch = vna::simulator::calculateBatchMeasurementTiming(points, 0, 2, 1);
    const auto tail = vna::simulator::calculateBatchMeasurementTiming(points, 2, 1, 2);
    const auto layoutHole = vna::simulator::calculateBatchMeasurementTiming(points, 0, 3, 1);

    ASSERT_TRUE(fullBatch.valid);
    ASSERT_TRUE(tail.valid);
    EXPECT_EQ(fullBatch.duration, std::chrono::milliseconds(2));
    EXPECT_EQ(tail.duration, std::chrono::microseconds(500));
    EXPECT_FALSE(layoutHole.valid);
}

TEST(SimulatorTimingTest, RejectsUnrepresentableBatchDuration)
{
    const std::vector<vna::simulator::Point> points{makePoint(1.0, std::numeric_limits<std::int64_t>::max(), {1})};

    const auto timing = vna::simulator::calculateBatchMeasurementTiming(points, 0, 1, 1);

    EXPECT_FALSE(timing.valid);
}

TEST(SimulatorTimingTest, RejectsAnIfBandwidthWhoseWindowCannotFitNanoseconds)
{
    const std::vector<vna::simulator::Point> points{makePoint(std::numeric_limits<double>::denorm_min(), 0, {1})};

    const auto timing = vna::simulator::calculateBatchMeasurementTiming(points, 0, 1, 1);

    EXPECT_FALSE(timing.valid);
}

TEST(SimulatorTimingTest, AcceptsAtMostOneHourOfLogicalSweepTime)
{
    constexpr std::int64_t oneSecondNanoseconds = 1000000000LL;
    const auto maximum = vna::simulator::simulatorMaximumSweepDuration.count();
    const std::vector<vna::simulator::Point> exact{makePoint(1.0, maximum - oneSecondNanoseconds, {1})};
    const std::vector<vna::simulator::Point> excessive{makePoint(1.0, maximum - oneSecondNanoseconds + 1, {1})};

    EXPECT_TRUE(vna::simulator::calculateSweepMeasurementTiming(exact).valid);
    EXPECT_FALSE(vna::simulator::calculateSweepMeasurementTiming(excessive).valid);
}

}  // namespace
