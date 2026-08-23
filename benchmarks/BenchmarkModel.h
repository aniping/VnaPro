#ifndef VNA_BENCHMARKS_BENCHMARK_MODEL_H
#define VNA_BENCHMARKS_BENCHMARK_MODEL_H

#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "VnaAcquisition.h"

namespace vna::benchmark {

inline constexpr std::size_t targetPointCount = 10000;
inline constexpr std::size_t targetExcitationCount = 4;
inline constexpr std::size_t targetWaveCount = 4;
inline constexpr std::size_t targetRecordCount = targetPointCount * targetExcitationCount;
inline constexpr std::size_t targetSampleCount = targetRecordCount * targetWaveCount;
inline constexpr std::size_t bytesPerRawSample = sizeof(std::complex<double>) + sizeof(std::uint32_t);
inline constexpr std::size_t targetPayloadBytes = targetSampleCount * bytesPerRawSample;

struct DurationSummary {
    double p50Seconds{0.0};
    double p95Seconds{0.0};
};

struct PipelineResult {
    double ifBandwidthHz{0.0};
    std::size_t warmupRuns{0};
    std::size_t measuredRuns{0};
    std::chrono::nanoseconds logicalDuration{0};
    DurationSummary elapsed;
};

struct AssemblyResult {
    std::size_t warmupFrames{0};
    std::size_t measuredFrames{0};
    DurationSummary copyOnly;
    DurationSummary completeFrame;
    double checksum{0.0};
};

VnaSweepPlan makeTargetPlan(double ifBandwidthHz);
PreparedSweep makeTargetPreparedSweep(double ifBandwidthHz);
std::chrono::nanoseconds targetLogicalDuration(double ifBandwidthHz);
DurationSummary summarizeDurations(std::vector<double> seconds);
std::vector<PipelineResult> runPipelineBenchmarks(
    const std::string &driverDirectory, const std::string &configurationFile);
AssemblyResult runAcquisitionAssemblyBenchmark();
void printBenchmarkReport(const std::vector<PipelineResult> &pipeline, const AssemblyResult &assembly);

}  // namespace vna::benchmark

#endif
