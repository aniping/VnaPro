#include "BenchmarkModel.h"

#include <exception>
#include <iostream>

int main()
{
    try {
        const auto pipeline =
            vna::benchmark::runPipelineBenchmarks(VNA_BENCHMARK_DRIVER_DIRECTORY, VNA_BENCHMARK_LOG_CONFIGURATION);
        const auto assembly = vna::benchmark::runAcquisitionAssemblyBenchmark();
        vna::benchmark::printBenchmarkReport(pipeline, assembly);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "benchmark_failed: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "benchmark_failed: unknown error\n";
        return 1;
    }
}
