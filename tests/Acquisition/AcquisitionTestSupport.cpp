#include "AcquisitionTestSupport.h"

#include <map>
#include <stdexcept>
#include <utility>

#include "PreparedMapping.h"
#include "VnaDriverSdk/VnaDriverAbi.h"

namespace vna::test {
namespace {

    VnaWaveKey incident(PortId port)
    {
        return VnaWaveKey{WaveKind::Incident, port};
    }

    VnaWaveKey outgoing(PortId port)
    {
        return VnaWaveKey{WaveKind::Outgoing, port};
    }

    VnaExcitationPlan excitation(ExcitationId id, PortId source, std::vector<VnaWaveKey> waves)
    {
        VnaExcitationPlan result;
        result.excitationId = id;
        result.sources.push_back(VnaSourcePlan{source, -10.0, 0.0});
        result.requiredWaves = std::move(waves);
        return result;
    }

    VnaPointPlan point(PointId id, double frequency, std::vector<VnaExcitationPlan> excitations)
    {
        VnaPointPlan result;
        result.pointId = id;
        result.frequencyHz = frequency;
        result.ifBandwidthHz = 1'000.0;
        result.excitations = std::move(excitations);
        return result;
    }

    std::vector<VnaWaveKey> wavesFor(ExcitationId excitationId)
    {
        if (excitationId == 40) {
            return {outgoing(3), incident(1)};
        }
        return {incident(1), outgoing(3)};
    }

    const VnaExcitationPlan &findExcitation(const VnaPointPlan &pointPlan, ExcitationId excitationId)
    {
        for (const auto &candidate : pointPlan.excitations) {
            if (candidate.excitationId == excitationId) {
                return candidate;
            }
        }
        throw std::logic_error("test plan does not contain the requested excitation");
    }

    WaveBlockSpec blockFor(
        const VnaSweepPlan &plan, ExcitationId excitationId, LayoutId layoutId,
        const std::vector<std::uint32_t> &pointIndices, SweepId sweepId)
    {
        WaveBlockSpec block;
        block.sweepId = sweepId;
        block.layoutId = layoutId;
        block.firstPointIndex = pointIndices.front();
        const auto &waves = findExcitation(plan.points[pointIndices.front()], excitationId).requiredWaves;
        block.waves.resize(waves.size());
        for (std::size_t wave = 0; wave < waves.size(); ++wave) {
            for (const auto pointIndex : pointIndices) {
                block.waves[wave].push_back(
                    expectedSample(excitationId, waves[wave], plan.points[pointIndex].pointId, sweepId));
            }
        }
        return block;
    }

    std::size_t recordCount(const VnaSweepPlan &plan)
    {
        std::size_t count = 0;
        for (const auto &pointPlan : plan.points) {
            count += pointPlan.excitations.size();
        }
        return count;
    }

}  // namespace

VnaSweepPlan makeSparsePlan()
{
    const auto excitation40 = excitation(40, 3, wavesFor(40));
    const auto excitation10 = excitation(10, 1, wavesFor(10));
    VnaSweepPlan plan;
    plan.points.push_back(point(503, 1.0e6, {excitation40, excitation10}));
    plan.points.push_back(point(9, 2.0e6, {excitation10}));
    plan.points.push_back(point(700, 3.0e6, {excitation10, excitation40}));
    plan.points.push_back(point(42, 4.0e6, {excitation10}));
    plan.points.push_back(point(301, 5.0e6, {excitation40}));
    return plan;
}

PreparedSweep makeSparsePrepared(const VnaSweepPlan &plan, ConfigurationId configurationId)
{
    PreparedSweep prepared;
    prepared.configurationId = configurationId;
    prepared.effectivePlan = plan;
    prepared.layouts.push_back(VnaWaveLayout{901, 40, wavesFor(40)});
    prepared.layouts.push_back(VnaWaveLayout{17, 10, wavesFor(10)});
    prepared.report.waveBlockPointCapacity = 4;
    prepared.report.physicalSweepCount = 1;
    prepared.report.atomic = true;
    prepared.report.phaseCoherent = true;
    return prepared;
}

AcquisitionLimits exactLimitsFor(const VnaSweepPlan &plan)
{
    std::size_t records = 0;
    std::size_t samples = 0;
    std::map<ExcitationId, std::size_t> waveCounts;
    for (const auto &pointPlan : plan.points) {
        records += pointPlan.excitations.size();
        for (const auto &item : pointPlan.excitations) {
            samples += item.requiredWaves.size();
            waveCounts[item.excitationId] = item.requiredWaves.size();
        }
    }
    std::size_t waves = 0;
    for (const auto &entry : waveCounts) {
        waves += entry.second;
    }
    AcquisitionLimits limits;
    limits.maximumRawFramePayloadBytes =
        samples * (sizeof(std::complex<double>) + sizeof(std::uint32_t)) + waveCounts.size() * sizeof(std::size_t);
    limits.maximumPreparedMappingPayloadBytes = records * sizeof(std::uint32_t) +
                                                waveCounts.size() * sizeof(detail::PreparedLayoutMapping) +
                                                waves * sizeof(detail::PreparedWaveMapping);
    return limits;
}

AcquisitionLimits spaciousLimits()
{
    return AcquisitionLimits{16U * 1024U * 1024U, 16U * 1024U * 1024U};
}

WaveSample expectedSample(ExcitationId excitationId, VnaWaveKey wave, PointId pointId, SweepId sweepId)
{
    const double kind = wave.kind == WaveKind::Outgoing ? 2'000.0 : 1'000.0;
    const double value = static_cast<double>(sweepId) * 1'000'000.0 + static_cast<double>(excitationId) * 10'000.0 +
                         kind + static_cast<double>(wave.port) * 100.0 + static_cast<double>(pointId);
    std::uint32_t quality = pointId % 2 == 0 ? VNA_QUALITY_OVERLOAD : VNA_QUALITY_VALID;
    if (wave.kind == WaveKind::Outgoing) {
        quality |= VNA_QUALITY_UNLOCKED;
    }
    return WaveSample{{value, -value - 0.5}, quality};
}

std::vector<WaveBlockSpec> makeSparseBlocks(const VnaSweepPlan &plan, SweepId sweepId)
{
    std::vector<WaveBlockSpec> blocks;
    blocks.push_back(blockFor(plan, 40, 901, {0}, sweepId));
    blocks.push_back(blockFor(plan, 40, 901, {2}, sweepId));
    blocks.push_back(blockFor(plan, 40, 901, {4}, sweepId));
    blocks.push_back(blockFor(plan, 10, 17, {0, 1, 2, 3}, sweepId));
    for (std::size_t sequence = 0; sequence < blocks.size(); ++sequence) {
        blocks[sequence].sequence = sequence;
    }
    return blocks;
}

bool completeSparseSweep(FakeVnaHardware &hardware, CommandId commandId, SweepId sweepId, const VnaSweepPlan &plan)
{
    hardware.publishStartSucceeded(commandId, sweepId);
    for (const auto &block : makeSparseBlocks(plan, sweepId)) {
        if (!hardware.publishWaveBlock(block)) {
            return false;
        }
    }
    const std::size_t records = recordCount(plan);
    hardware.publishSweepFinished(
        SweepCompletion{sweepId, SweepTerminationReason::Completed, records, records, 0}, DeviceState::Prepared);
    return true;
}

VnaError testError(VnaErrorCode code, const char *description)
{
    VnaError error;
    error.code = code;
    error.description = description;
    return error;
}

void AcquisitionTest::SetUp()
{
    plan_ = makeSparsePlan();
    resetEngine(true, spaciousLimits());
}

void AcquisitionTest::resetEngine(bool supportsAbort, AcquisitionLimits limits)
{
    engine_.reset();
    hardware_ = std::make_shared<FakeVnaHardware>(supportsAbort);
    auto created = createAcquisitionEngine(hardware_, sink_, limits);
    if (!created.hasValue()) {
        throw std::runtime_error(created.error().description);
    }
    engine_ = created.takeValue();
}

CommandId AcquisitionTest::prepareSuccessfully(ConfigurationId configurationId)
{
    prepared_ = makeSparsePrepared(plan_, configurationId);
    const std::size_t expectedNotices = sink_.preparedCount() + 1;
    auto submitted = engine_->prepareAsync(plan_);
    if (!submitted.hasValue()) {
        throw std::runtime_error(submitted.error().description);
    }
    const CommandId commandId = submitted.value();
    hardware_->publishPrepareSucceeded(commandId, prepared_);
    if (!sink_.waitForPrepared(expectedNotices) || !sink_.prepared(expectedNotices - 1).prepared) {
        throw std::runtime_error("test prepare did not complete successfully");
    }
    return commandId;
}

bool AcquisitionTest::completeCurrentSweep(SweepId sweepId)
{
    return completeSparseSweep(*hardware_, hardware_->lastStartCommand(), sweepId, plan_);
}

}  // namespace vna::test
