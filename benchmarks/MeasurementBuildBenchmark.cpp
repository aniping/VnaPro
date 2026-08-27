#include "PreparedMapping.h"
#include "VnaMeasurement.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vna::benchmark {
namespace {

    using BenchmarkClock = std::chrono::steady_clock;
    constexpr std::size_t portCount = 4;
    constexpr std::size_t pointCount = 10'000;
    constexpr std::size_t wavesPerExcitation = portCount + 1U;
    constexpr std::size_t ratioCount = portCount * portCount;
    constexpr std::size_t warmupFrames = 20;
    constexpr std::size_t measuredFrames = 200;
    constexpr std::size_t bytesPerMiB = 1024U * 1024U;

    template <typename T>
    T requireValue(Result<T> result, const char *operation)
    {
        if (!result.hasValue()) {
            throw std::runtime_error(std::string{operation} + ": " + result.error().description);
        }
        return result.takeValue();
    }

    std::vector<VnaExcitationPlan> makeExcitations()
    {
        std::vector<VnaExcitationPlan> excitations;
        excitations.reserve(portCount);
        for (PortId port = 1; port <= portCount; ++port) {
            VnaExcitationPlan excitation;
            excitation.excitationId = static_cast<ExcitationId>(port * 10U);
            excitation.sources.push_back(VnaSourcePlan{port, -10.0, 0.0});
            excitations.push_back(std::move(excitation));
        }
        return excitations;
    }

    MeasurementRequest makeRequest()
    {
        MeasurementRequest request;
        const auto excitations = makeExcitations();
        request.sweepTemplate.points.reserve(pointCount);
        for (std::size_t index = 0; index < pointCount; ++index) {
            VnaPointPlan point;
            point.pointId = static_cast<PointId>(index + 1U);
            point.frequencyHz = 1.0e6 + static_cast<double>(index);
            point.ifBandwidthHz = 1'000.0;
            point.excitations = excitations;
            request.sweepTemplate.points.push_back(std::move(point));
        }
        ObservationId observationId = 1;
        for (PortId source = 1; source <= portCount; ++source) {
            for (PortId output = 1; output <= portCount; ++output) {
                request.observations.push_back(
                    ObservationDefinition{
                        observationId++, static_cast<ExcitationId>(source * 10U),
                        WaveRatioExpression{{WaveKind::Outgoing, output}, {WaveKind::Incident, source}}});
            }
        }
        return request;
    }

    PreparedSweep makePreparedSweep(VnaSweepPlan plan)
    {
        if (plan.points.empty()) {
            throw std::runtime_error("compiled sweep plan is empty");
        }
        PreparedSweep prepared;
        prepared.configurationId = 1;
        LayoutId layoutId = 1;
        for (const auto &excitation : plan.points.front().excitations) {
            prepared.layouts.push_back(VnaWaveLayout{layoutId++, excitation.excitationId, excitation.requiredWaves});
        }
        prepared.effectivePlan = std::move(plan);
        prepared.report.physicalSweepCount = 1;
        prepared.report.waveBlockPointCapacity = pointCount;
        prepared.report.atomic = false;
        prepared.report.phaseCoherent = true;
        return prepared;
    }

    std::complex<double> sampleValue(ExcitationId excitationId, VnaWaveKey wave, std::size_t pointIndex)
    {
        const double base = 10.0 + static_cast<double>(excitationId) + static_cast<double>(pointIndex) * 1.0e-4;
        if (wave.kind == WaveKind::Incident) {
            return {base, 1.0 + static_cast<double>(wave.port)};
        }
        const double scale = 1.0 + static_cast<double>(wave.port) * 0.25;
        return {base * scale, -base * 0.5};
    }

    void fillRawAssembly(const PreparedAcquisition &prepared, detail::RawFrameAssembly &assembly)
    {
        auto layouts = prepared.hardwarePrepared().layouts;
        std::sort(layouts.begin(), layouts.end(), [](const auto &left, const auto &right) {
            return left.layoutId < right.layoutId;
        });
        std::size_t destination = 0;
        for (std::size_t layoutIndex = 0; layoutIndex < layouts.size(); ++layoutIndex) {
            const auto &layout = layouts[layoutIndex];
            assembly.nextExpectedPointOffsets[layoutIndex] = pointCount;
            assembly.copiedPointExcitationRecordCount += pointCount;
            for (const auto wave : layout.waveKeys) {
                for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
                    assembly.values.at(destination) = sampleValue(layout.excitationId, wave, pointIndex);
                    assembly.quality.at(destination) = 0;
                    ++destination;
                }
            }
        }
        assembly.copiedWaveSampleCount = destination;
    }

    std::shared_ptr<const RawSweepFrame> makeRawFrame(std::shared_ptr<const PreparedAcquisition> acquisition)
    {
        auto assembly = requireValue(detail::allocateRawFrameAssembly(*acquisition), "raw allocation");
        fillRawAssembly(*acquisition, assembly);
        return requireValue(
            detail::freezeRawSweepFrame(1, 0, 1, std::move(acquisition), std::move(assembly)), "raw freeze");
    }

    struct BuildInputs {
        std::shared_ptr<const PreparedMeasurement> prepared;
        std::shared_ptr<const RawSweepFrame> raw;
        std::size_t rawBytes{0};
        std::size_t observationBytes{0};
    };

    void validateShape(const PreparedAcquisition &acquisition, const PreparedMeasurement &prepared)
    {
        const auto &hardware = acquisition.hardwarePrepared();
        if ((hardware.effectivePlan.points.size() != pointCount) || (hardware.layouts.size() != portCount) ||
            (acquisition.pointExcitationRecordCount() != pointCount * portCount) ||
            (acquisition.waveSampleCountPerSweep() != pointCount * portCount * wavesPerExcitation) ||
            (prepared.compiled().observationCount() != ratioCount) ||
            (prepared.observationSampleCount() != pointCount * ratioCount)) {
            throw std::runtime_error("compiled benchmark shape does not match the 4P/10k recipe");
        }
        for (const auto &layout : hardware.layouts) {
            if (layout.waveKeys.size() != wavesPerExcitation) {
                throw std::runtime_error("prepared benchmark layout does not contain five waves");
            }
        }
    }

    BuildInputs makeBuildInputs()
    {
        const MeasurementLimits measurementLimits{64U * bytesPerMiB, 64U * bytesPerMiB};
        const AcquisitionLimits acquisitionLimits{64U * bytesPerMiB, 64U * bytesPerMiB};
        auto compilation = requireValue(compileMeasurement(makeRequest(), measurementLimits), "measurement compile");
        auto hardwarePrepared =
            std::make_shared<const PreparedSweep>(makePreparedSweep(std::move(compilation.sweepPlan)));
        auto acquisition = requireValue(
            detail::compilePreparedAcquisition(std::move(hardwarePrepared), acquisitionLimits), "acquisition prepare");
        auto prepared = requireValue(bindMeasurement(compilation.compiled, acquisition), "measurement bind");
        validateShape(*acquisition, *prepared);
        const std::size_t rawBytes =
            acquisition->waveSampleCountPerSweep() * (sizeof(std::complex<double>) + sizeof(std::uint32_t));
        const std::size_t observationBytes =
            prepared->observationSampleCount() * (sizeof(std::complex<double>) + sizeof(ObservationSampleStatus));
        return BuildInputs{std::move(prepared), makeRawFrame(std::move(acquisition)), rawBytes, observationBytes};
    }

    double frameChecksum(const ObservationFrame &frame)
    {
        double checksum = 0.0;
        for (ObservationId id = 1; id <= ratioCount; ++id) {
            const auto series = frame.findObservation(id);
            if (!series || (series->size != pointCount)) {
                throw std::runtime_error("observation frame shape is invalid");
            }
            checksum += series->values[0].real();
            checksum += series->values[series->size - 1U].imag();
            checksum += static_cast<double>(series->status[series->size - 1U].calculationQuality);
        }
        return checksum;
    }

    class BuildFixture final {
    public:
        BuildFixture() : inputs_(makeBuildInputs())
        {
        }

        double measureFrame()
        {
            const auto startedAt = BenchmarkClock::now();
            auto frame = buildObservationFrame(inputs_.prepared, *inputs_.raw);
            const auto completedAt = BenchmarkClock::now();
            const auto built = requireValue(std::move(frame), "observation build");
            checksum_ += frameChecksum(*built);
            return std::chrono::duration<double>(completedAt - startedAt).count();
        }

        std::size_t rawBytes() const noexcept
        {
            return inputs_.rawBytes;
        }
        std::size_t observationBytes() const noexcept
        {
            return inputs_.observationBytes;
        }
        double checksum() const noexcept
        {
            return checksum_;
        }

    private:
        BuildInputs inputs_;
        double checksum_{0.0};
    };

    struct DurationSummary {
        double p50Seconds{0.0};
        double p95Seconds{0.0};
        double p99Seconds{0.0};
        double totalSeconds{0.0};
    };

    double percentile(const std::vector<double> &sorted, double fraction)
    {
        const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
        return sorted.at(std::max<std::size_t>(1, rank) - 1U);
    }

    DurationSummary summarize(std::vector<double> samples)
    {
        DurationSummary summary;
        for (const double sample : samples) {
            summary.totalSeconds += sample;
        }
        std::sort(samples.begin(), samples.end());
        summary.p50Seconds = percentile(samples, 0.50);
        summary.p95Seconds = percentile(samples, 0.95);
        summary.p99Seconds = percentile(samples, 0.99);
        return summary;
    }

    double throughput(std::size_t bytesPerFrame, double totalSeconds)
    {
        return static_cast<double>(bytesPerFrame * measuredFrames) / static_cast<double>(bytesPerMiB) / totalSeconds;
    }

    void printReport(const BuildFixture &fixture, const DurationSummary &summary)
    {
        const std::size_t combinedBytes = fixture.rawBytes() + fixture.observationBytes();
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "VnaMeasurementBenchmark scope=buildObservationFrame-only\n";
        std::cout << "shape ports=" << portCount << " points=" << pointCount << " excitations=" << portCount
                  << " waves_per_excitation=" << wavesPerExcitation << " ratios=" << ratioCount << '\n';
        std::cout << "warmup_frames=" << warmupFrames << " measured_frames=" << measuredFrames << '\n';
        std::cout << "latency_ms p50=" << summary.p50Seconds * 1'000.0 << " p95=" << summary.p95Seconds * 1'000.0
                  << " p99=" << summary.p99Seconds * 1'000.0 << '\n';
        std::cout << "throughput_mib_per_s input_raw=" << throughput(fixture.rawBytes(), summary.totalSeconds)
                  << " output_observation=" << throughput(fixture.observationBytes(), summary.totalSeconds)
                  << " combined=" << throughput(combinedBytes, summary.totalSeconds) << '\n';
        std::cout << std::setprecision(12) << "checksum=" << fixture.checksum() << '\n';
    }

    int runBenchmark()
    {
        BuildFixture fixture;
        for (std::size_t index = 0; index < warmupFrames; ++index) {
            (void)fixture.measureFrame();
        }
        std::vector<double> samples;
        samples.reserve(measuredFrames);
        for (std::size_t index = 0; index < measuredFrames; ++index) {
            samples.push_back(fixture.measureFrame());
        }
        printReport(fixture, summarize(std::move(samples)));
        return 0;
    }

}  // namespace
}  // namespace vna::benchmark

int main()
{
    try {
        return vna::benchmark::runBenchmark();
    } catch (const std::exception &error) {
        std::cerr << "VnaMeasurementBenchmark failed: " << error.what() << '\n';
        return 1;
    }
}
