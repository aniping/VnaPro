#include "MeasurementTestSupport.h"

#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

#include "VnaWaveData.h"

namespace vna::test {
namespace {

    struct BoundFixture {
        MeasurementCompilation compilation;
        std::shared_ptr<const PreparedAcquisition> acquisition;
        std::shared_ptr<const PreparedMeasurement> prepared;
    };

    BoundFixture bindRequest(MeasurementRequest request, ConfigurationId configurationId)
    {
        auto compiled = compileMeasurement(std::move(request), spaciousMeasurementLimits());
        if (!compiled.hasValue()) {
            throw std::logic_error(compiled.error().description);
        }
        BoundFixture fixture;
        fixture.compilation = compiled.takeValue();
        auto hardwarePrepared = makeMeasurementPreparedSweep(fixture.compilation.sweepPlan, configurationId);
        auto acquisition = makePreparedAcquisition(hardwarePrepared);
        if (!acquisition.hasValue()) {
            throw std::logic_error(acquisition.error().description);
        }
        fixture.acquisition = acquisition.takeValue();
        auto bound = bindMeasurement(fixture.compilation.compiled, fixture.acquisition);
        if (!bound.hasValue()) {
            throw std::logic_error(bound.error().description);
        }
        fixture.prepared = bound.takeValue();
        return fixture;
    }

    MeasurementWaveSample deterministicSample(ExcitationId excitationId, VnaWaveKey wave, std::uint32_t pointIndex)
    {
        const double kind = wave.kind == WaveKind::Incident ? 10.0 : 20.0;
        const double real = static_cast<double>(excitationId) + kind + static_cast<double>(wave.port * 3U) +
                            static_cast<double>(pointIndex);
        return MeasurementWaveSample{{real, -real / 2.0}, 0};
    }

    std::uint32_t qualityBit(WaveQuality quality)
    {
        return static_cast<std::uint32_t>(quality);
    }

    void expectUnavailable(std::complex<double> value)
    {
        EXPECT_TRUE(std::isnan(value.real()));
        EXPECT_TRUE(std::isnan(value.imag()));
    }

    MeasurementWaveSample qualitySample(ExcitationId, VnaWaveKey wave, std::uint32_t pointIndex)
    {
        const double infinity = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double maximum = std::numeric_limits<double>::max();
        const double minimum = std::numeric_limits<double>::min();
        const auto numeratorQuality = qualityBit(WaveQuality::Overload) | qualityBit(WaveQuality::Unlocked) |
                                      qualityBit(WaveQuality::SourceUnsettled);
        const std::array<MeasurementWaveSample, 6> numeratorSamples{{
            {{4.0, 2.0}, numeratorQuality},
            {{nan, nan}, qualityBit(WaveQuality::Missing)},
            {{1.0, 0.0}, 0},
            {{infinity, 0.0}, 0},
            {{1.0, 0.0}, 0},
            {{maximum, maximum}, 0},
        }};
        const std::array<MeasurementWaveSample, 6> denominatorSamples{{
            {{2.0, 0.0}, qualityBit(WaveQuality::ReferenceLow)},
            {{0.0, 0.0}, 0},
            {{0.0, 0.0}, qualityBit(WaveQuality::Missing)},
            {{nan, 1.0}, 0},
            {{0.0, 0.0}, 0},
            {{minimum, 0.0}, 0},
        }};
        return wave.kind == WaveKind::Outgoing ? numeratorSamples.at(pointIndex) : denominatorSamples.at(pointIndex);
    }

}  // namespace

TEST(ObservationBuilderTest, BuildsDeterministicSparseDirectAndRatioSeries)
{
    auto fixture = bindRequest(makeSparseMeasurementRequest(), 501);
    auto raw = makeRawSweepFrame(fixture.acquisition, 71, 3, 9001, deterministicSample);
    ASSERT_TRUE(raw.hasValue()) << raw.error().description;

    auto built = buildObservationFrame(fixture.prepared, *raw.value());

    ASSERT_TRUE(built.hasValue()) << built.error().description;
    const auto &frame = *built.value();
    EXPECT_EQ(frame.runId(), 71U);
    EXPECT_EQ(frame.sweepIndex(), 3U);
    EXPECT_EQ(frame.hardwareSweepId(), 9001U);
    EXPECT_EQ(&frame.prepared(), fixture.prepared.get());
    EXPECT_FALSE(frame.findObservation(999).has_value());

    const auto direct = frame.findObservation(42);
    ASSERT_TRUE(direct.has_value());
    ASSERT_EQ(direct->size, 3U);
    EXPECT_EQ(direct->pointIndices[0], 0U);
    EXPECT_EQ(direct->pointIndices[1], 2U);
    EXPECT_EQ(direct->pointIndices[2], 4U);
    for (std::size_t index = 0; index < direct->size; ++index) {
        EXPECT_EQ(direct->values[index], deterministicSample(40, outgoingWave(3), direct->pointIndices[index]).value);
        EXPECT_EQ(direct->status[index].calculationQuality, 0U);
    }

    const auto ratio = frame.findObservation(101);
    ASSERT_TRUE(ratio.has_value());
    ASSERT_EQ(ratio->size, 4U);
    for (std::size_t index = 0; index < ratio->size; ++index) {
        const auto pointIndex = ratio->pointIndices[index];
        const auto expected = deterministicSample(10, outgoingWave(1), pointIndex).value /
                              deterministicSample(10, incidentWave(1), pointIndex).value;
        EXPECT_EQ(ratio->values[index], expected);
    }
}

TEST(ObservationBuilderTest, AppliesQualityTruthTableWithoutFailingTheFrame)
{
    auto fixture = bindRequest(makeQualityMeasurementRequest(6), 502);
    auto raw = makeRawSweepFrame(fixture.acquisition, 72, 4, 9002, qualitySample);
    ASSERT_TRUE(raw.hasValue()) << raw.error().description;

    auto built = buildObservationFrame(fixture.prepared, *raw.value());

    ASSERT_TRUE(built.hasValue()) << built.error().description;
    const auto direct = built.value()->findObservation(9);
    const auto ratio = built.value()->findObservation(10);
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(ratio.has_value());
    ASSERT_EQ(ratio->size, 6U);

    EXPECT_EQ(direct->values[0], std::complex<double>(4.0, 2.0));
    const auto numeratorQuality = qualityBit(WaveQuality::Overload) | qualityBit(WaveQuality::Unlocked) |
                                  qualityBit(WaveQuality::SourceUnsettled);
    EXPECT_EQ(direct->status[0].numeratorWaveQuality, numeratorQuality);
    EXPECT_EQ(direct->status[0].denominatorWaveQuality, 0U);
    EXPECT_EQ(direct->status[0].calculationQuality, 0U);
    expectUnavailable(direct->values[1]);
    EXPECT_EQ(direct->status[1].calculationQuality, static_cast<std::uint32_t>(CalculationQuality::InputUnavailable));
    expectUnavailable(direct->values[3]);
    EXPECT_EQ(direct->status[3].calculationQuality, static_cast<std::uint32_t>(CalculationQuality::ValueNonFinite));

    EXPECT_EQ(ratio->values[0], std::complex<double>(2.0, 1.0));
    EXPECT_EQ(ratio->status[0].numeratorWaveQuality, numeratorQuality);
    EXPECT_EQ(ratio->status[0].denominatorWaveQuality, qualityBit(WaveQuality::ReferenceLow));
    EXPECT_EQ(ratio->status[0].calculationQuality, 0U);

    const auto unavailable = static_cast<std::uint32_t>(CalculationQuality::InputUnavailable);
    const auto numeratorNonFinite = static_cast<std::uint32_t>(CalculationQuality::ValueNonFinite);
    const auto denominatorNonFinite = static_cast<std::uint32_t>(CalculationQuality::DenominatorNonFinite);
    const auto denominatorZero = static_cast<std::uint32_t>(CalculationQuality::DenominatorZero);
    const auto resultNonFinite = static_cast<std::uint32_t>(CalculationQuality::ResultNonFinite);
    EXPECT_EQ(ratio->status[1].calculationQuality, unavailable | denominatorZero);
    EXPECT_EQ(ratio->status[2].calculationQuality, unavailable);
    EXPECT_EQ(ratio->status[3].calculationQuality, numeratorNonFinite | denominatorNonFinite);
    EXPECT_EQ(ratio->status[4].calculationQuality, denominatorZero);
    EXPECT_EQ(ratio->status[5].calculationQuality, resultNonFinite);
    for (std::size_t index = 1; index < ratio->size; ++index) {
        expectUnavailable(ratio->values[index]);
    }
}

TEST(ObservationBuilderTest, RejectsRawFramesFromAnotherPreparedAcquisition)
{
    auto first = bindRequest(makeSparseMeasurementRequest(), 503);
    auto second = bindRequest(makeSparseMeasurementRequest(), 504);
    auto raw = makeRawSweepFrame(second.acquisition, 73, 0, 9003, deterministicSample);
    ASSERT_TRUE(raw.hasValue());

    auto built = buildObservationFrame(first.prepared, *raw.value());

    ASSERT_FALSE(built.hasValue());
    EXPECT_EQ(built.error().code, VnaErrorCode::ConfigurationExpired);
}

TEST(ObservationBuilderTest, RepeatedBuildsAreIndependentAndKeepOldPreparedAlive)
{
    auto fixture = bindRequest(makeSparseMeasurementRequest(), 505);
    auto raw = makeRawSweepFrame(fixture.acquisition, 74, 8, 9004, deterministicSample);
    ASSERT_TRUE(raw.hasValue());
    auto first = buildObservationFrame(fixture.prepared, *raw.value());
    auto second = buildObservationFrame(fixture.prepared, *raw.value());
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());

    const auto firstSeries = first.value()->findObservation(101);
    const auto secondSeries = second.value()->findObservation(101);
    ASSERT_TRUE(firstSeries.has_value());
    ASSERT_TRUE(secondSeries.has_value());
    ASSERT_EQ(firstSeries->size, secondSeries->size);
    EXPECT_NE(firstSeries->values, secondSeries->values);
    EXPECT_NE(firstSeries->status, secondSeries->status);
    for (std::size_t index = 0; index < firstSeries->size; ++index) {
        EXPECT_EQ(firstSeries->values[index], secondSeries->values[index]);
    }

    fixture.prepared.reset();
    fixture.acquisition.reset();
    raw.value().reset();
    EXPECT_EQ(first.value()->prepared().acquisition().hardwarePrepared().configurationId, 505U);
    EXPECT_TRUE(first.value()->findObservation(42).has_value());
}

TEST(ObservationBuilderTest, RejectsNullPreparedPointer)
{
    auto fixture = bindRequest(makeSparseMeasurementRequest(), 506);
    auto raw = makeRawSweepFrame(fixture.acquisition, 75, 0, 9005, deterministicSample);
    ASSERT_TRUE(raw.hasValue());

    auto built = buildObservationFrame(nullptr, *raw.value());

    ASSERT_FALSE(built.hasValue());
    EXPECT_EQ(built.error().code, VnaErrorCode::InvalidPlan);
}

}  // namespace vna::test
