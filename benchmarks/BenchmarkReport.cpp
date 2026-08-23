#include "BenchmarkModel.h"

#include <chrono>
#include <iomanip>
#include <iostream>

namespace vna::benchmark {
namespace {

    constexpr double bytesPerMebibyte = 1024.0 * 1024.0;

    double logicalSeconds(const PipelineResult &result)
    {
        return std::chrono::duration<double>(result.logicalDuration).count();
    }

    double rate(std::size_t units, double seconds)
    {
        return static_cast<double>(units) / seconds;
    }

    double mebibytesPerSecond(double seconds)
    {
        return (static_cast<double>(targetPayloadBytes) / bytesPerMebibyte) / seconds;
    }

    void printPipelineResult(const PipelineResult &result)
    {
        const double logical = logicalSeconds(result);
        const double p50Ratio = result.elapsed.p50Seconds / logical;
        const double p95Ratio = result.elapsed.p95Seconds / logical;
        std::cout << "pipeline"
                  << " ifbw_hz=" << result.ifBandwidthHz << " warmup_runs=" << result.warmupRuns
                  << " measured_runs=" << result.measuredRuns << " logical_ms=" << logical * 1000.0
                  << " p50_ms=" << result.elapsed.p50Seconds * 1000.0
                  << " p95_ms=" << result.elapsed.p95Seconds * 1000.0 << " p50_wall_over_logical=" << p50Ratio
                  << " p95_wall_over_logical=" << p95Ratio << " p50_excess_percent=" << (p50Ratio - 1.0) * 100.0
                  << " p95_excess_percent=" << (p95Ratio - 1.0) * 100.0
                  << " p50_points_per_s=" << rate(targetPointCount, result.elapsed.p50Seconds)
                  << " p95_points_per_s=" << rate(targetPointCount, result.elapsed.p95Seconds)
                  << " p50_point_excitations_per_s=" << rate(targetRecordCount, result.elapsed.p50Seconds)
                  << " p95_point_excitations_per_s=" << rate(targetRecordCount, result.elapsed.p95Seconds)
                  << " p50_complex_samples_per_s=" << rate(targetSampleCount, result.elapsed.p50Seconds)
                  << " p95_complex_samples_per_s=" << rate(targetSampleCount, result.elapsed.p95Seconds)
                  << " p50_payload_mib_per_s=" << mebibytesPerSecond(result.elapsed.p50Seconds)
                  << " p95_payload_mib_per_s=" << mebibytesPerSecond(result.elapsed.p95Seconds) << '\n';
    }

    void printAssemblyResult(const AssemblyResult &result)
    {
        std::cout << "assembly"
                  << " warmup_frames=" << result.warmupFrames << " measured_frames=" << result.measuredFrames
                  << " measured_payload_gib="
                  << (static_cast<double>(targetPayloadBytes * result.measuredFrames) / (bytesPerMebibyte * 1024.0))
                  << " copy_p50_ms=" << result.copyOnly.p50Seconds * 1000.0
                  << " copy_p95_ms=" << result.copyOnly.p95Seconds * 1000.0
                  << " copy_p50_msamples_per_s=" << rate(targetSampleCount, result.copyOnly.p50Seconds) / 1.0e6
                  << " copy_p95_msamples_per_s=" << rate(targetSampleCount, result.copyOnly.p95Seconds) / 1.0e6
                  << " copy_p50_payload_mib_per_s=" << mebibytesPerSecond(result.copyOnly.p50Seconds)
                  << " copy_p95_payload_mib_per_s=" << mebibytesPerSecond(result.copyOnly.p95Seconds)
                  << " frame_p50_ms=" << result.completeFrame.p50Seconds * 1000.0
                  << " frame_p95_ms=" << result.completeFrame.p95Seconds * 1000.0
                  << " frame_p50_msamples_per_s=" << rate(targetSampleCount, result.completeFrame.p50Seconds) / 1.0e6
                  << " frame_p95_msamples_per_s=" << rate(targetSampleCount, result.completeFrame.p95Seconds) / 1.0e6
                  << " frame_p50_payload_mib_per_s=" << mebibytesPerSecond(result.completeFrame.p50Seconds)
                  << " frame_p95_payload_mib_per_s=" << mebibytesPerSecond(result.completeFrame.p95Seconds)
                  << " checksum=" << result.checksum << '\n';
    }

}  // namespace

void printBenchmarkReport(const std::vector<PipelineResult> &pipeline, const AssemblyResult &assembly)
{
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "benchmark configuration=" << VNA_BENCHMARK_CONFIGURATION
              << " clock=steady_clock percentile=linear_interpolation\n";
    std::cout << "shape points=" << targetPointCount << " excitations_per_point=" << targetExcitationCount
              << " waves_per_excitation=" << targetWaveCount << " complex_samples=" << targetSampleCount
              << " bytes_per_sample=" << bytesPerRawSample << " payload_bytes=" << targetPayloadBytes << '\n';
    for (const auto &result : pipeline) {
        printPipelineResult(result);
    }
    printAssemblyResult(assembly);
}

}  // namespace vna::benchmark
