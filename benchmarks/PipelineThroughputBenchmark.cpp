#include "BenchmarkModel.h"

#include <array>
#include <utility>
#include <vector>

#include "PipelineBenchmarkSession.h"

namespace vna::benchmark {
namespace {

    struct PipelineScenario {
        double ifBandwidthHz{0.0};
        std::size_t warmupRuns{0};
        std::size_t measuredRuns{0};
    };

    PipelineResult runScenario(PipelineBenchmarkSession &session, PipelineScenario scenario)
    {
        session.prepare(scenario.ifBandwidthHz);
        for (std::size_t index = 0; index < scenario.warmupRuns; ++index) {
            (void)session.measureSweepSeconds();
        }
        std::vector<double> elapsed;
        elapsed.reserve(scenario.measuredRuns);
        for (std::size_t index = 0; index < scenario.measuredRuns; ++index) {
            elapsed.push_back(session.measureSweepSeconds());
        }
        PipelineResult result;
        result.ifBandwidthHz = scenario.ifBandwidthHz;
        result.warmupRuns = scenario.warmupRuns;
        result.measuredRuns = scenario.measuredRuns;
        result.logicalDuration = targetLogicalDuration(scenario.ifBandwidthHz);
        result.elapsed = summarizeDurations(std::move(elapsed));
        return result;
    }

}  // namespace

std::vector<PipelineResult> runPipelineBenchmarks(
    const std::string &driverDirectory, const std::string &configurationFile)
{
    const std::array<PipelineScenario, 3> scenarios{
        PipelineScenario{10000.0, 1, 9}, PipelineScenario{30000.0, 1, 9}, PipelineScenario{10.0e6, 3, 30}};
    PipelineBenchmarkSession session(driverDirectory, configurationFile);
    std::vector<PipelineResult> results;
    results.reserve(scenarios.size());
    for (const auto scenario : scenarios) {
        results.push_back(runScenario(session, scenario));
    }
    return results;
}

}  // namespace vna::benchmark
