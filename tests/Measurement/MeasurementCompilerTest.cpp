#include "MeasurementTestSupport.h"

#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

namespace vna::test {
namespace {

    const VnaExcitationPlan &findExcitation(const VnaPointPlan &point, ExcitationId id)
    {
        for (const auto &candidate : point.excitations) {
            if (candidate.excitationId == id) {
                return candidate;
            }
        }
        throw std::logic_error("excitation not found");
    }

    void expectWave(VnaWaveKey actual, WaveKind kind, PortId port)
    {
        EXPECT_EQ(actual.kind, kind);
        EXPECT_EQ(actual.port, port);
    }

    void expectCompileError(MeasurementRequest request, VnaErrorCode code)
    {
        const auto result = compileMeasurement(std::move(request), spaciousMeasurementLimits());
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, code);
    }

}  // namespace

TEST(MeasurementCompilerTest, CompilesCanonicalDependenciesAndPreservesSweepFields)
{
    MeasurementRequest request = makeSparseMeasurementRequest();
    const auto sourceAtSecondPoint = request.sweepTemplate.points[1].excitations[0].sources[0];

    auto result = compileMeasurement(std::move(request), spaciousMeasurementLimits());

    ASSERT_TRUE(result.hasValue()) << result.error().description;
    auto compilation = result.takeValue();
    ASSERT_NE(compilation.compiled, nullptr);
    EXPECT_EQ(compilation.compiled->observationCount(), 4U);
    EXPECT_EQ(compilation.compiled->observation(0).id, 42U);
    EXPECT_EQ(compilation.compiled->observation(1).id, 101U);
    EXPECT_EQ(compilation.compiled->observation(2).id, 102U);
    EXPECT_EQ(compilation.compiled->observation(3).id, 120U);
    EXPECT_THROW(compilation.compiled->observation(4), std::out_of_range);

    const auto &waves10 = findExcitation(compilation.sweepPlan.points[0], 10).requiredWaves;
    ASSERT_EQ(waves10.size(), 3U);
    expectWave(waves10[0], WaveKind::Incident, 1);
    expectWave(waves10[1], WaveKind::Outgoing, 1);
    expectWave(waves10[2], WaveKind::Outgoing, 2);
    const auto &waves40 = findExcitation(compilation.sweepPlan.points[0], 40).requiredWaves;
    ASSERT_EQ(waves40.size(), 1U);
    expectWave(waves40[0], WaveKind::Outgoing, 3);

    EXPECT_EQ(findExcitation(compilation.sweepPlan.points[1], 10).requiredWaves.size(), 3U);
    EXPECT_EQ(compilation.sweepPlan.trigger.mode, TriggerMode::External);
    EXPECT_TRUE(compilation.sweepPlan.policy.allowQuantization);
    EXPECT_TRUE(compilation.sweepPlan.policy.allowMultiplePhysicalSweeps);
    EXPECT_TRUE(compilation.sweepPlan.policy.requirePhaseCoherence);
    EXPECT_FALSE(compilation.sweepPlan.policy.requireAtomicConfiguration);
    const auto &retainedSource = compilation.sweepPlan.points[1].excitations[0].sources[0];
    EXPECT_EQ(retainedSource.port, sourceAtSecondPoint.port);
    EXPECT_DOUBLE_EQ(retainedSource.powerDbm, sourceAtSecondPoint.powerDbm);
    EXPECT_DOUBLE_EQ(retainedSource.phaseDegrees, sourceAtSecondPoint.phaseDegrees);
}

TEST(MeasurementCompilerTest, DirectOnlyRequestDoesNotTightenPhasePolicy)
{
    MeasurementRequest request = makeSparseMeasurementRequest();
    request.observations.erase(request.observations.begin());
    request.observations.erase(request.observations.begin() + 1);

    auto result = compileMeasurement(std::move(request), spaciousMeasurementLimits());

    ASSERT_TRUE(result.hasValue()) << result.error().description;
    EXPECT_FALSE(result.value().sweepPlan.policy.requirePhaseCoherence);
}

TEST(MeasurementCompilerTest, RejectsInvalidObservationDefinitions)
{
    auto empty = makeSparseMeasurementRequest();
    empty.observations.clear();
    expectCompileError(std::move(empty), VnaErrorCode::InvalidPlan);

    auto zeroId = makeSparseMeasurementRequest();
    zeroId.observations[0].id = 0;
    expectCompileError(std::move(zeroId), VnaErrorCode::InvalidPlan);

    auto duplicateId = makeSparseMeasurementRequest();
    duplicateId.observations[1].id = duplicateId.observations[0].id;
    expectCompileError(std::move(duplicateId), VnaErrorCode::InvalidPlan);

    auto duplicateExpression = makeSparseMeasurementRequest();
    auto duplicate = duplicateExpression.observations[0];
    duplicate.id = 500;
    duplicateExpression.observations.push_back(duplicate);
    expectCompileError(std::move(duplicateExpression), VnaErrorCode::InvalidPlan);

    auto unknownExcitation = makeSparseMeasurementRequest();
    unknownExcitation.observations[0].excitationId = 99;
    expectCompileError(std::move(unknownExcitation), VnaErrorCode::InvalidPlan);
}

TEST(MeasurementCompilerTest, RejectsUnusedExcitationsAndNonemptyTemplateWaves)
{
    auto unused = makeSparseMeasurementRequest();
    unused.observations.erase(unused.observations.begin() + 1);
    expectCompileError(std::move(unused), VnaErrorCode::InvalidPlan);

    auto populated = makeSparseMeasurementRequest();
    populated.sweepTemplate.points[0].excitations[0].requiredWaves.push_back(incidentWave(3));
    expectCompileError(std::move(populated), VnaErrorCode::InvalidPlan);
}

TEST(MeasurementCompilerTest, RejectsInvalidWaveKeysWithoutApplyingDevicePortLimits)
{
    auto zeroPort = makeSparseMeasurementRequest();
    std::get<WaveRatioExpression>(zeroPort.observations[0].expression).numerator.port = 0;
    expectCompileError(std::move(zeroPort), VnaErrorCode::InvalidPlan);

    auto invalidKind = makeSparseMeasurementRequest();
    std::get<DirectWaveExpression>(invalidKind.observations[1].expression).wave.kind = static_cast<WaveKind>(99);
    expectCompileError(std::move(invalidKind), VnaErrorCode::InvalidPlan);

    auto deviceSpecificPort = makeSparseMeasurementRequest();
    std::get<DirectWaveExpression>(deviceSpecificPort.observations[1].expression).wave.port = 500;
    auto result = compileMeasurement(std::move(deviceSpecificPort), spaciousMeasurementLimits());
    EXPECT_TRUE(result.hasValue()) << result.error().description;
}

TEST(MeasurementCompilerTest, RejectsMinimumInputShapeBeforeAllocatingInspectionCollections)
{
    auto request = makeSparseMeasurementRequest();
    const auto repeatedObservation = request.observations.front();
    request.observations.resize(4096, repeatedObservation);
    const MeasurementLimits limits{1, 1};

    auto result = compileMeasurement(std::move(request), limits);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, VnaErrorCode::UnsupportedPlan);
}

}  // namespace vna::test
