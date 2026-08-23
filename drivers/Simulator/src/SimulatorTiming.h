#ifndef VNA_DRIVER_SIMULATOR_SIMULATOR_TIMING_H
#define VNA_DRIVER_SIMULATOR_SIMULATOR_TIMING_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "SimulatorDevice.h"

namespace vna::simulator {

inline constexpr std::chrono::nanoseconds simulatorMaximumSweepDuration{3600000000000LL};

struct BatchMeasurementTiming {
    bool valid{false};
    std::chrono::nanoseconds duration{0};
};

BatchMeasurementTiming calculateBatchMeasurementTiming(
    const std::vector<Point> &points, std::size_t firstPoint, std::uint32_t pointCount,
    std::uint32_t layoutId) noexcept;
BatchMeasurementTiming calculateSweepMeasurementTiming(const std::vector<Point> &points) noexcept;

}  // namespace vna::simulator

#endif
