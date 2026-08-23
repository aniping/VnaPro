#ifndef VNA_BENCHMARKS_PIPELINE_BENCHMARK_SESSION_H
#define VNA_BENCHMARKS_PIPELINE_BENCHMARK_SESSION_H

#include <memory>
#include <string>

namespace vna::benchmark {

class PipelineBenchmarkSession final {
public:
    PipelineBenchmarkSession(const std::string &driverDirectory, const std::string &configurationFile);
    ~PipelineBenchmarkSession();
    PipelineBenchmarkSession(const PipelineBenchmarkSession &) = delete;
    PipelineBenchmarkSession &operator=(const PipelineBenchmarkSession &) = delete;
    PipelineBenchmarkSession(PipelineBenchmarkSession &&) = delete;
    PipelineBenchmarkSession &operator=(PipelineBenchmarkSession &&) = delete;

    void prepare(double ifBandwidthHz);
    double measureSweepSeconds();

private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace vna::benchmark

#endif
