#include "MeasurementTestSupport.h"

#include <memory>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

namespace vna::test {
namespace {

    struct CompiledFixture {
        MeasurementCompilation compilation;
        PreparedSweep hardwarePrepared;
    };

    CompiledFixture compileFixture(ConfigurationId configurationId)
    {
        auto compiled = compileMeasurement(makeSparseMeasurementRequest(), spaciousMeasurementLimits());
        if (!compiled.hasValue()) {
            throw std::logic_error(compiled.error().description);
        }
        CompiledFixture fixture;
        fixture.compilation = compiled.takeValue();
        fixture.hardwarePrepared = makeMeasurementPreparedSweep(fixture.compilation.sweepPlan, configurationId);
        return fixture;
    }

    Result<std::shared_ptr<const PreparedMeasurement>> bindFixture(const PreparedSweep &hardwarePrepared)
    {
        auto fixture = compileFixture(hardwarePrepared.configurationId);
        auto acquisition = makePreparedAcquisition(hardwarePrepared);
        if (!acquisition.hasValue()) {
            return Result<std::shared_ptr<const PreparedMeasurement>>::failure(acquisition.error());
        }
        return bindMeasurement(fixture.compilation.compiled, acquisition.takeValue());
    }

}  // namespace

TEST(MeasurementBinderTest, BindsSparseLayoutsByIdentityAndCountsOutputSamples)
{
    auto fixture = compileFixture(401);
    ASSERT_GT(fixture.hardwarePrepared.layouts.size(), 1U);
    EXPECT_GT(
        fixture.hardwarePrepared.layouts.front().excitationId, fixture.hardwarePrepared.layouts.back().excitationId);
    auto acquisition = makePreparedAcquisition(fixture.hardwarePrepared);
    ASSERT_TRUE(acquisition.hasValue()) << acquisition.error().description;

    auto bound = bindMeasurement(fixture.compilation.compiled, acquisition.takeValue());

    ASSERT_TRUE(bound.hasValue()) << bound.error().description;
    EXPECT_EQ(bound.value()->observationSampleCount(), 15U);
    EXPECT_EQ(&bound.value()->compiled(), fixture.compilation.compiled.get());
    EXPECT_EQ(bound.value()->acquisition().hardwarePrepared().configurationId, 401U);
}

TEST(MeasurementBinderTest, RejectsNullInputs)
{
    auto fixture = compileFixture(402);
    auto acquisition = makePreparedAcquisition(fixture.hardwarePrepared);
    ASSERT_TRUE(acquisition.hasValue());

    auto missingCompiled = bindMeasurement(nullptr, acquisition.value());
    ASSERT_FALSE(missingCompiled.hasValue());
    EXPECT_EQ(missingCompiled.error().code, VnaErrorCode::InvalidPlan);

    auto missingAcquisition = bindMeasurement(fixture.compilation.compiled, nullptr);
    ASSERT_FALSE(missingAcquisition.hasValue());
    EXPECT_EQ(missingAcquisition.error().code, VnaErrorCode::InvalidPlan);
}

TEST(MeasurementBinderTest, RejectsEffectivePlanFieldChanges)
{
    auto fixture = compileFixture(403);
    fixture.hardwarePrepared.effectivePlan.points[1].frequencyHz += 1.0;
    auto frequencyMismatch = bindFixture(fixture.hardwarePrepared);
    ASSERT_FALSE(frequencyMismatch.hasValue());
    EXPECT_EQ(frequencyMismatch.error().code, VnaErrorCode::ConfigurationRejected);

    fixture = compileFixture(404);
    fixture.hardwarePrepared.effectivePlan.points[2].excitations[0].sources[0].phaseDegrees += 0.25;
    auto sourceMismatch = bindFixture(fixture.hardwarePrepared);
    ASSERT_FALSE(sourceMismatch.hasValue());
    EXPECT_EQ(sourceMismatch.error().code, VnaErrorCode::ConfigurationRejected);

    fixture = compileFixture(405);
    fixture.hardwarePrepared.effectivePlan.policy.allowMultiplePhysicalSweeps = false;
    auto policyMismatch = bindFixture(fixture.hardwarePrepared);
    ASSERT_FALSE(policyMismatch.hasValue());
    EXPECT_EQ(policyMismatch.error().code, VnaErrorCode::ConfigurationRejected);
}

TEST(MeasurementBinderTest, RejectsQuantizedOrNoncoherentPrepareReports)
{
    auto fixture = compileFixture(406);
    fixture.hardwarePrepared.report.quantized = true;
    auto quantized = bindFixture(fixture.hardwarePrepared);
    ASSERT_FALSE(quantized.hasValue());
    EXPECT_EQ(quantized.error().code, VnaErrorCode::ProtocolError);

    fixture = compileFixture(407);
    fixture.hardwarePrepared.report.phaseCoherent = false;
    auto noncoherent = bindFixture(fixture.hardwarePrepared);
    ASSERT_FALSE(noncoherent.hasValue());
    EXPECT_EQ(noncoherent.error().code, VnaErrorCode::ProtocolError);
}

TEST(MeasurementBinderTest, RejectsLayoutsThatContradictAnUnchangedEffectivePlan)
{
    auto fixture = compileFixture(408);
    auto mutablePrepared = std::make_shared<PreparedSweep>(fixture.hardwarePrepared);
    std::shared_ptr<const PreparedSweep> frozen = mutablePrepared;
    auto acquisition = detail::compilePreparedAcquisition(frozen, spaciousAcquisitionLimits());
    ASSERT_TRUE(acquisition.hasValue()) << acquisition.error().description;
    mutablePrepared->layouts.pop_back();

    auto missingLayout = bindMeasurement(fixture.compilation.compiled, acquisition.takeValue());

    ASSERT_FALSE(missingLayout.hasValue());
    EXPECT_EQ(missingLayout.error().code, VnaErrorCode::ProtocolError);
}

TEST(MeasurementBinderTest, RejectsLayoutWaveChangesAsProtocolErrors)
{
    auto fixture = compileFixture(409);
    auto mutablePrepared = std::make_shared<PreparedSweep>(fixture.hardwarePrepared);
    std::shared_ptr<const PreparedSweep> frozen = mutablePrepared;
    auto acquisition = detail::compilePreparedAcquisition(frozen, spaciousAcquisitionLimits());
    ASSERT_TRUE(acquisition.hasValue()) << acquisition.error().description;
    ASSERT_FALSE(mutablePrepared->layouts.front().waveKeys.empty());
    mutablePrepared->layouts.front().waveKeys.pop_back();

    auto missingWave = bindMeasurement(fixture.compilation.compiled, acquisition.takeValue());

    ASSERT_FALSE(missingWave.hasValue());
    EXPECT_EQ(missingWave.error().code, VnaErrorCode::ProtocolError);
}

}  // namespace vna::test
