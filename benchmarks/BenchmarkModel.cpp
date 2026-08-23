#include "BenchmarkModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vna::benchmark {
namespace {

    VnaWaveKey wave(WaveKind kind, PortId port)
    {
        return VnaWaveKey{kind, port};
    }

    VnaExcitationPlan makeExcitation(ExcitationId identifier)
    {
        const PortId source = identifier;
        const PortId neighbor = source == targetExcitationCount ? 1 : static_cast<PortId>(source + 1);
        VnaExcitationPlan excitation;
        excitation.excitationId = identifier;
        excitation.sources.push_back(VnaSourcePlan{source, -10.0, 0.0});
        excitation.requiredWaves = {
            wave(WaveKind::Incident, source), wave(WaveKind::Outgoing, source), wave(WaveKind::Incident, neighbor),
            wave(WaveKind::Outgoing, neighbor)};
        return excitation;
    }

    std::vector<VnaExcitationPlan> makeExcitations()
    {
        std::vector<VnaExcitationPlan> excitations;
        excitations.reserve(targetExcitationCount);
        for (std::size_t index = 0; index < targetExcitationCount; ++index) {
            excitations.push_back(makeExcitation(static_cast<ExcitationId>(index + 1)));
        }
        return excitations;
    }

    VnaPointPlan makePoint(std::size_t index, double ifBandwidthHz, const std::vector<VnaExcitationPlan> &excitations)
    {
        VnaPointPlan point;
        point.pointId = static_cast<PointId>(index + 1);
        point.frequencyHz = 1.0e6 + (static_cast<double>(index) * 1.0e6);
        point.ifBandwidthHz = ifBandwidthHz;
        point.dwellTime = std::chrono::nanoseconds::zero();
        point.excitations = excitations;
        return point;
    }

    std::chrono::nanoseconds measurementWindow(double ifBandwidthHz)
    {
        constexpr long double nanosecondsPerSecond = 1000000000.0L;
        if (!std::isfinite(ifBandwidthHz) || (ifBandwidthHz <= 0.0)) {
            throw std::invalid_argument("IF bandwidth must be positive and finite");
        }
        const long double rounded = std::ceil(nanosecondsPerSecond / ifBandwidthHz);
        if (rounded >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("measurement window is not representable");
        }
        return std::chrono::nanoseconds{static_cast<std::int64_t>(rounded)};
    }

}  // namespace

VnaSweepPlan makeTargetPlan(double ifBandwidthHz)
{
    const auto excitations = makeExcitations();
    VnaSweepPlan plan;
    plan.points.reserve(targetPointCount);
    for (std::size_t index = 0; index < targetPointCount; ++index) {
        plan.points.push_back(makePoint(index, ifBandwidthHz, excitations));
    }
    return plan;
}

PreparedSweep makeTargetPreparedSweep(double ifBandwidthHz)
{
    PreparedSweep prepared;
    prepared.configurationId = 1;
    prepared.effectivePlan = makeTargetPlan(ifBandwidthHz);
    const auto &firstPoint = prepared.effectivePlan.points.front();
    for (const auto &excitation : firstPoint.excitations) {
        prepared.layouts.push_back(
            VnaWaveLayout{
                static_cast<LayoutId>(excitation.excitationId), excitation.excitationId, excitation.requiredWaves});
    }
    prepared.report.physicalSweepCount = 1;
    prepared.report.waveBlockPointCapacity = targetPointCount;
    prepared.report.atomic = true;
    prepared.report.phaseCoherent = true;
    return prepared;
}

std::chrono::nanoseconds targetLogicalDuration(double ifBandwidthHz)
{
    const auto window = measurementWindow(ifBandwidthHz);
    const auto count = static_cast<std::int64_t>(targetRecordCount);
    if (window.count() > (std::numeric_limits<std::int64_t>::max() / count)) {
        throw std::overflow_error("target logical duration is not representable");
    }
    return std::chrono::nanoseconds{window.count() * count};
}

DurationSummary summarizeDurations(std::vector<double> seconds)
{
    if (seconds.empty()) {
        throw std::invalid_argument("duration sample set is empty");
    }
    std::sort(seconds.begin(), seconds.end());
    const auto percentile = [&seconds](double quantile) {
        const double position = quantile * static_cast<double>(seconds.size() - 1);
        const auto lower = static_cast<std::size_t>(std::floor(position));
        const auto upper = static_cast<std::size_t>(std::ceil(position));
        const double weight = position - static_cast<double>(lower);
        return seconds[lower] + ((seconds[upper] - seconds[lower]) * weight);
    };
    return DurationSummary{percentile(0.50), percentile(0.95)};
}

}  // namespace vna::benchmark
