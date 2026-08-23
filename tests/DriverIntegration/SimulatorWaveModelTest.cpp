#include "SimulatorDevice.h"

#include <cmath>
#include <complex>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

vna::simulator::Point makeNoisePoint(double ifBandwidthHz, std::uint32_t pointId = 1)
{
    vna::simulator::Point point;
    point.identifier = pointId;
    point.frequencyHz = 1.0e9 + (static_cast<double>(pointId) * 1.0e6);
    point.ifBandwidthHz = ifBandwidthHz;
    return point;
}

vna::simulator::Excitation makeSourceExcitation()
{
    vna::simulator::Excitation excitation;
    excitation.identifier = 1;
    excitation.layoutId = 1;
    excitation.sources.push_back(vna::simulator::Source{1, 0.0, 0.0});
    return excitation;
}

std::complex<double> asComplex(const VnaComplex64V1 &value)
{
    return std::complex<double>{value.real, value.imag};
}

double measuredNoiseRms(double ifBandwidthHz)
{
    constexpr std::uint64_t sampleCount = 4096;
    const auto point = makeNoisePoint(ifBandwidthHz);
    const auto excitation = makeSourceExcitation();
    const vna::simulator::Wave unsourcedReceiver{VNA_WAVE_INCIDENT, 4};
    long double energy = 0.0L;
    for (std::uint64_t sweepId = 1; sweepId <= sampleCount; ++sweepId) {
        energy += std::norm(asComplex(vna::simulator::simulateWave(sweepId, point, excitation, unsourcedReceiver)));
    }
    return std::sqrt(static_cast<double>(energy / sampleCount));
}

double measuredOpenRatioTraceNoiseDb()
{
    constexpr std::uint64_t sampleCount = 4096;
    const auto point = makeNoisePoint(10000.0);
    const auto excitation = makeSourceExcitation();
    const vna::simulator::Wave incident{VNA_WAVE_INCIDENT, 1};
    const vna::simulator::Wave reflected{VNA_WAVE_OUTGOING, 1};
    long double sum = 0.0L;
    long double squaredSum = 0.0L;
    for (std::uint64_t sweepId = 1; sweepId <= sampleCount; ++sweepId) {
        const auto a = asComplex(vna::simulator::simulateWave(sweepId, point, excitation, incident));
        const auto b = asComplex(vna::simulator::simulateWave(sweepId, point, excitation, reflected));
        const double decibels = 20.0 * std::log10(std::abs(b / a));
        sum += decibels;
        squaredSum += decibels * decibels;
    }
    const long double mean = sum / sampleCount;
    return std::sqrt(static_cast<double>((squaredSum / sampleCount) - (mean * mean)));
}

std::complex<double> measuredIsolationMean()
{
    constexpr std::uint64_t sampleCount = 4096;
    const auto point = makeNoisePoint(10000.0);
    const auto excitation = makeSourceExcitation();
    const vna::simulator::Wave isolated{VNA_WAVE_OUTGOING, 2};
    std::complex<long double> sum{0.0L, 0.0L};
    for (std::uint64_t sweepId = 1; sweepId <= sampleCount; ++sweepId) {
        const auto sample = asComplex(vna::simulator::simulateWave(sweepId, point, excitation, isolated));
        sum += std::complex<long double>{sample.real(), sample.imag()};
    }
    return std::complex<double>{
        static_cast<double>(sum.real() / sampleCount), static_cast<double>(sum.imag() / sampleCount)};
}

TEST(SimulatorWaveModelTest, IsStableWithinASampleButChangesAcrossSweepsAndPoints)
{
    const auto firstPoint = makeNoisePoint(10000.0, 1);
    const auto secondPoint = makeNoisePoint(10000.0, 2);
    const auto excitation = makeSourceExcitation();
    const vna::simulator::Wave wave{VNA_WAVE_INCIDENT, 4};

    const auto first = asComplex(vna::simulator::simulateWave(7, firstPoint, excitation, wave));
    const auto repeated = asComplex(vna::simulator::simulateWave(7, firstPoint, excitation, wave));
    const auto nextSweep = asComplex(vna::simulator::simulateWave(8, firstPoint, excitation, wave));
    const auto nextPoint = asComplex(vna::simulator::simulateWave(7, secondPoint, excitation, wave));

    EXPECT_EQ(first, repeated);
    EXPECT_NE(first, nextSweep);
    EXPECT_NE(first, nextPoint);
}

TEST(SimulatorWaveModelTest, NoiseRmsFollowsTheSquareRootOfIfBandwidth)
{
    const double oneKilohertz = measuredNoiseRms(1000.0);
    const double tenKilohertz = measuredNoiseRms(10000.0);

    EXPECT_NEAR(tenKilohertz / oneKilohertz, std::sqrt(10.0), 0.15);
}

TEST(SimulatorWaveModelTest, NoiseRmsMatchesTheDeclaredAmplitudeDensity)
{
    constexpr double expectedTenKilohertzRms = 3.162277660168379e-6;

    EXPECT_NEAR(measuredNoiseRms(10000.0), expectedTenKilohertzRms, 2.0e-7);
}

TEST(SimulatorWaveModelTest, OpenRatioHasOneMillidecibelTraceNoiseAtTenKilohertz)
{
    EXPECT_NEAR(measuredOpenRatioTraceNoiseDb(), 0.001, 0.0001);
}

TEST(SimulatorWaveModelTest, BuiltInOpenHasNearUnityReflectionAndFiniteIsolation)
{
    const auto point = makeNoisePoint(1000.0);
    const auto excitation = makeSourceExcitation();
    const vna::simulator::Wave reflected{VNA_WAVE_OUTGOING, 1};

    const auto reflection = asComplex(vna::simulator::simulateWave(17, point, excitation, reflected));
    const auto isolationMean = measuredIsolationMean();

    EXPECT_GT(std::abs(reflection), 0.98);
    EXPECT_LT(std::abs(reflection), 1.01);
    EXPECT_GT(std::abs(isolationMean), 7.0e-6);
    EXPECT_LT(std::abs(isolationMean), 1.3e-5);
}

}  // namespace
