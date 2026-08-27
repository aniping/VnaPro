#include "MeasurementTestSupport.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "PreparedMapping.h"

namespace vna::test {
namespace {

    VnaSourcePlan source(PortId port, double power, double phase)
    {
        return VnaSourcePlan{port, power, phase};
    }

    VnaExcitationPlan excitation(ExcitationId id, VnaSourcePlan sourcePlan)
    {
        VnaExcitationPlan result;
        result.excitationId = id;
        result.sources.push_back(sourcePlan);
        return result;
    }

    VnaPointPlan point(PointId id, double frequency, std::vector<VnaExcitationPlan> excitations)
    {
        VnaPointPlan result;
        result.pointId = id;
        result.frequencyHz = frequency;
        result.ifBandwidthHz = 1'000.0 + static_cast<double>(id % 7U);
        result.dwellTime = std::chrono::nanoseconds(100 + id);
        result.excitations = std::move(excitations);
        return result;
    }

    const VnaExcitationPlan *findExcitation(const VnaPointPlan &pointPlan, ExcitationId id)
    {
        for (const auto &candidate : pointPlan.excitations) {
            if (candidate.excitationId == id) {
                return &candidate;
            }
        }
        return nullptr;
    }

    std::vector<std::uint32_t> pointIndicesFor(const VnaSweepPlan &plan, ExcitationId id)
    {
        std::vector<std::uint32_t> indices;
        for (std::size_t index = 0; index < plan.points.size(); ++index) {
            if (findExcitation(plan.points[index], id) != nullptr) {
                indices.push_back(static_cast<std::uint32_t>(index));
            }
        }
        return indices;
    }

    std::vector<const VnaWaveLayout *> sortedLayouts(const PreparedSweep &prepared)
    {
        std::vector<const VnaWaveLayout *> layouts;
        for (const auto &layout : prepared.layouts) {
            layouts.push_back(&layout);
        }
        std::sort(layouts.begin(), layouts.end(), [](const auto *left, const auto *right) {
            return left->layoutId < right->layoutId;
        });
        return layouts;
    }

    void fillRawAssembly(
        const PreparedAcquisition &prepared, const MeasurementSampleProvider &provider,
        detail::RawFrameAssembly &assembly)
    {
        const auto layouts = sortedLayouts(prepared.hardwarePrepared());
        std::size_t destination = 0;
        for (std::size_t layoutIndex = 0; layoutIndex < layouts.size(); ++layoutIndex) {
            const auto *layout = layouts[layoutIndex];
            const auto indices = pointIndicesFor(prepared.hardwarePrepared().effectivePlan, layout->excitationId);
            assembly.nextExpectedPointOffsets[layoutIndex] = indices.size();
            assembly.copiedPointExcitationRecordCount += indices.size();
            for (const auto wave : layout->waveKeys) {
                for (const auto pointIndex : indices) {
                    const auto sample = provider(layout->excitationId, wave, pointIndex);
                    assembly.values.at(destination) = sample.value;
                    assembly.quality.at(destination) = sample.quality;
                    ++destination;
                }
            }
        }
        assembly.copiedWaveSampleCount = destination;
    }

}  // namespace

VnaWaveKey incidentWave(PortId port)
{
    return VnaWaveKey{WaveKind::Incident, port};
}

VnaWaveKey outgoingWave(PortId port)
{
    return VnaWaveKey{WaveKind::Outgoing, port};
}

MeasurementLimits spaciousMeasurementLimits()
{
    return MeasurementLimits{64U * 1024U * 1024U, 64U * 1024U * 1024U};
}

AcquisitionLimits spaciousAcquisitionLimits()
{
    return AcquisitionLimits{64U * 1024U * 1024U, 64U * 1024U * 1024U};
}

MeasurementRequest makeSparseMeasurementRequest()
{
    MeasurementRequest request;
    request.sweepTemplate.points.push_back(
        point(503, 1.0e6, {excitation(40, source(3, -10.0, 5.0)), excitation(10, source(1, -12.0, 0.0))}));
    request.sweepTemplate.points.push_back(point(9, 2.0e6, {excitation(10, source(1, -11.0, 0.5))}));
    request.sweepTemplate.points.push_back(
        point(700, 3.0e6, {excitation(10, source(2, -9.0, 1.0)), excitation(40, source(3, -8.0, 6.0))}));
    request.sweepTemplate.points.push_back(point(42, 4.0e6, {excitation(10, source(1, -7.0, 1.5))}));
    request.sweepTemplate.points.push_back(point(301, 5.0e6, {excitation(40, source(4, -6.0, 7.0))}));
    request.sweepTemplate.trigger.mode = TriggerMode::External;
    request.sweepTemplate.policy.allowQuantization = true;
    request.sweepTemplate.policy.allowMultiplePhysicalSweeps = true;
    request.sweepTemplate.policy.requirePhaseCoherence = false;
    request.sweepTemplate.policy.requireAtomicConfiguration = false;
    request.observations.push_back(
        ObservationDefinition{102, 10, WaveRatioExpression{outgoingWave(2), incidentWave(1)}});
    request.observations.push_back(ObservationDefinition{42, 40, DirectWaveExpression{outgoingWave(3)}});
    request.observations.push_back(
        ObservationDefinition{101, 10, WaveRatioExpression{outgoingWave(1), incidentWave(1)}});
    request.observations.push_back(ObservationDefinition{120, 10, DirectWaveExpression{incidentWave(1)}});
    return request;
}

MeasurementRequest makeQualityMeasurementRequest(std::size_t pointCount)
{
    MeasurementRequest request;
    for (std::size_t index = 0; index < pointCount; ++index) {
        request.sweepTemplate.points.push_back(point(
            static_cast<PointId>(100U + index), 1.0e6 + static_cast<double>(index),
            {excitation(10, source(1, -10.0, 0.0))}));
    }
    request.observations.push_back(ObservationDefinition{9, 10, DirectWaveExpression{outgoingWave(1)}});
    request.observations.push_back(
        ObservationDefinition{10, 10, WaveRatioExpression{outgoingWave(1), incidentWave(1)}});
    return request;
}

PreparedSweep makeMeasurementPreparedSweep(const VnaSweepPlan &plan, ConfigurationId configurationId)
{
    std::map<ExcitationId, std::vector<VnaWaveKey>> definitions;
    for (const auto &pointPlan : plan.points) {
        for (const auto &item : pointPlan.excitations) {
            definitions.emplace(item.excitationId, item.requiredWaves);
        }
    }
    PreparedSweep prepared;
    prepared.configurationId = configurationId;
    prepared.effectivePlan = plan;
    LayoutId layoutId = 700;
    for (auto iterator = definitions.rbegin(); iterator != definitions.rend(); ++iterator) {
        prepared.layouts.push_back(VnaWaveLayout{layoutId, iterator->first, iterator->second});
        layoutId -= 113;
    }
    prepared.report.physicalSweepCount = 1;
    prepared.report.waveBlockPointCapacity = plan.points.size();
    prepared.report.atomic = plan.policy.requireAtomicConfiguration;
    prepared.report.phaseCoherent = true;
    return prepared;
}

Result<std::shared_ptr<const PreparedAcquisition>> makePreparedAcquisition(
    const PreparedSweep &prepared, AcquisitionLimits limits)
{
    return detail::compilePreparedAcquisition(std::make_shared<const PreparedSweep>(prepared), limits);
}

Result<std::shared_ptr<const RawSweepFrame>> makeRawSweepFrame(
    std::shared_ptr<const PreparedAcquisition> prepared, AcquisitionRunId runId, std::uint64_t sweepIndex,
    SweepId sweepId, const MeasurementSampleProvider &provider)
{
    auto allocated = detail::allocateRawFrameAssembly(*prepared);
    if (!allocated.hasValue()) {
        return Result<std::shared_ptr<const RawSweepFrame>>::failure(allocated.error());
    }
    auto assembly = allocated.takeValue();
    fillRawAssembly(*prepared, provider, assembly);
    return detail::freezeRawSweepFrame(runId, sweepIndex, sweepId, std::move(prepared), std::move(assembly));
}

}  // namespace vna::test
