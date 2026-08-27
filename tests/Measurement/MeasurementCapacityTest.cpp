#include "MeasurementTestSupport.h"

#include <cmath>
#include <complex>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

#include "MeasurementInternal.h"
#include "PreparedMapping.h"

namespace vna::test {
namespace {

    MeasurementRequest makeFourPortRequest(std::size_t pointCount)
    {
        MeasurementRequest request;
        request.sweepTemplate.points.reserve(pointCount);
        for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
            VnaPointPlan point;
            point.pointId = static_cast<PointId>(500'001U + pointIndex);
            point.frequencyHz = 1.0e6 + static_cast<double>(pointIndex);
            point.ifBandwidthHz = 1'000.0;
            for (PortId port = 1; port <= 4; ++port) {
                VnaExcitationPlan excitation;
                excitation.excitationId = static_cast<ExcitationId>(port * 10U);
                excitation.sources.push_back(VnaSourcePlan{port, -10.0, 0.0});
                point.excitations.push_back(std::move(excitation));
            }
            request.sweepTemplate.points.push_back(std::move(point));
        }
        ObservationId id = 1;
        for (PortId sourcePort = 1; sourcePort <= 4; ++sourcePort) {
            for (PortId outputPort = 1; outputPort <= 4; ++outputPort) {
                request.observations.push_back(
                    ObservationDefinition{
                        id++, static_cast<ExcitationId>(sourcePort * 10U),
                        WaveRatioExpression{outgoingWave(outputPort), incidentWave(sourcePort)}});
            }
        }
        return request;
    }

    detail::MeasurementPayload exactPayload(const MeasurementRequest &request)
    {
        auto inspected = detail::inspectMeasurementRequest(request, spaciousMeasurementLimits());
        if (!inspected.hasValue()) {
            throw std::logic_error(inspected.error().description);
        }
        auto payload = detail::inspectMeasurementPayload(inspected.value().shape, spaciousMeasurementLimits());
        if (!payload.hasValue()) {
            throw std::logic_error(payload.error().description);
        }
        return payload.value();
    }

    MeasurementWaveSample largeShapeSample(ExcitationId excitationId, VnaWaveKey wave, std::uint32_t pointIndex)
    {
        const double base = 100.0 + static_cast<double>(excitationId) + static_cast<double>(wave.port) +
                            static_cast<double>(pointIndex) * 1.0e-4;
        if (wave.kind == WaveKind::Incident) {
            return MeasurementWaveSample{{base, 1.0}, 0};
        }
        return MeasurementWaveSample{{base * 2.0, -base}, 0};
    }

}  // namespace

TEST(MeasurementCapacityTest, ExactLogicalBudgetsPassAndOneByteShortFails)
{
    const auto payload = exactPayload(makeSparseMeasurementRequest());
    ASSERT_GT(payload.preparedBytes, 0U);
    ASSERT_GT(payload.buildBytes, 0U);

    auto exact = compileMeasurement(
        makeSparseMeasurementRequest(), MeasurementLimits{payload.preparedBytes, payload.buildBytes});
    ASSERT_TRUE(exact.hasValue()) << exact.error().description;

    auto preparedShort = compileMeasurement(
        makeSparseMeasurementRequest(), MeasurementLimits{payload.preparedBytes - 1U, payload.buildBytes});
    ASSERT_FALSE(preparedShort.hasValue());
    EXPECT_EQ(preparedShort.error().code, VnaErrorCode::UnsupportedPlan);

    auto buildShort = compileMeasurement(
        makeSparseMeasurementRequest(), MeasurementLimits{payload.preparedBytes, payload.buildBytes - 1U});
    ASSERT_FALSE(buildShort.hasValue());
    EXPECT_EQ(buildShort.error().code, VnaErrorCode::UnsupportedPlan);
}

TEST(MeasurementCapacityTest, ZeroBudgetsAreInvalid)
{
    auto preparedZero = compileMeasurement(makeSparseMeasurementRequest(), MeasurementLimits{0, 1});
    ASSERT_FALSE(preparedZero.hasValue());
    EXPECT_EQ(preparedZero.error().code, VnaErrorCode::InvalidPlan);

    auto buildZero = compileMeasurement(makeSparseMeasurementRequest(), MeasurementLimits{1, 0});
    ASSERT_FALSE(buildZero.hasValue());
    EXPECT_EQ(buildZero.error().code, VnaErrorCode::InvalidPlan);
}

TEST(MeasurementCapacityTest, FourPortTenThousandPointShapeMatchesDesignBaseline)
{
    const auto request = makeFourPortRequest(10'000);
    auto inspected = detail::inspectMeasurementRequest(request, spaciousMeasurementLimits());
    ASSERT_TRUE(inspected.hasValue()) << inspected.error().description;
    const auto &shape = inspected.value().shape;
    EXPECT_EQ(shape.pointCount, 10'000U);
    EXPECT_EQ(shape.excitationOccurrenceCount, 40'000U);
    EXPECT_EQ(shape.sourceCount, 40'000U);
    EXPECT_EQ(shape.excitationCount, 4U);
    EXPECT_EQ(shape.dependencyCount, 20U);
    EXPECT_EQ(shape.observationCount, 16U);
    EXPECT_EQ(shape.pointIndexCount, 40'000U);
    EXPECT_EQ(shape.observationSampleCount, 160'000U);

    auto payload = detail::inspectMeasurementPayload(shape, spaciousMeasurementLimits());
    ASSERT_TRUE(payload.hasValue()) << payload.error().description;
    const std::size_t frameBytes =
        shape.observationSampleCount * (sizeof(std::complex<double>) + sizeof(ObservationSampleStatus));
    EXPECT_EQ(frameBytes, 4'480'000U);
    EXPECT_EQ(payload.value().buildBytes, frameBytes + 20U * sizeof(RawWaveSeriesView));
}

TEST(MeasurementCapacityTest, ArithmeticOverflowFailsClosed)
{
    detail::MeasurementShape shape;
    shape.pointCount = std::numeric_limits<std::size_t>::max();
    shape.excitationOccurrenceCount = 1;
    shape.sourceCount = 1;
    shape.excitationCount = 1;
    shape.dependencyCount = 1;
    shape.observationCount = 1;
    shape.pointIndexCount = 1;
    shape.observationSampleCount = 1;

    auto payload = detail::inspectMeasurementPayload(shape, spaciousMeasurementLimits());

    ASSERT_FALSE(payload.hasValue());
    EXPECT_EQ(payload.error().code, VnaErrorCode::UnsupportedPlan);
}

TEST(MeasurementCapacityTest, FourPortBaselineBuildsAtExactAcquisitionBudget)
{
    auto compiled = compileMeasurement(makeFourPortRequest(10'000), spaciousMeasurementLimits());
    ASSERT_TRUE(compiled.hasValue()) << compiled.error().description;
    auto compilation = compiled.takeValue();
    const auto hardwarePrepared = makeMeasurementPreparedSweep(compilation.sweepPlan, 610);
    auto inspected = detail::inspectRequestedPlan(compilation.sweepPlan, spaciousAcquisitionLimits());
    ASSERT_TRUE(inspected.hasValue()) << inspected.error().description;
    const auto &acquisitionShape = inspected.value();
    EXPECT_EQ(acquisitionShape.rawFramePayloadBytes, 4'000'000U + 4U * sizeof(std::size_t));

    const AcquisitionLimits exact{acquisitionShape.rawFramePayloadBytes, acquisitionShape.preparedMappingPayloadBytes};
    AcquisitionLimits oneByteShort = exact;
    --oneByteShort.maximumRawFramePayloadBytes;
    auto rejected = makePreparedAcquisition(hardwarePrepared, oneByteShort);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, VnaErrorCode::UnsupportedPlan);

    auto acquisition = makePreparedAcquisition(hardwarePrepared, exact);
    ASSERT_TRUE(acquisition.hasValue()) << acquisition.error().description;
    auto prepared = bindMeasurement(compilation.compiled, acquisition.value());
    ASSERT_TRUE(prepared.hasValue()) << prepared.error().description;
    EXPECT_EQ(prepared.value()->observationSampleCount(), 160'000U);
    auto raw = makeRawSweepFrame(acquisition.takeValue(), 91, 0, 6'101, largeShapeSample);
    ASSERT_TRUE(raw.hasValue()) << raw.error().description;

    auto frame = buildObservationFrame(prepared.takeValue(), *raw.value());

    ASSERT_TRUE(frame.hasValue()) << frame.error().description;
    const auto firstRatio = frame.value()->findObservation(1);
    ASSERT_TRUE(firstRatio.has_value());
    EXPECT_EQ(firstRatio->size, 10'000U);
    EXPECT_TRUE(std::isfinite(firstRatio->values[9'999].real()));
    EXPECT_TRUE(std::isfinite(firstRatio->values[9'999].imag()));
}

}  // namespace vna::test
