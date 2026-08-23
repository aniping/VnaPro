#ifndef VNA_HARDWARE_VNA_DRIVER_LIMITS_H
#define VNA_HARDWARE_VNA_DRIVER_LIMITS_H

#include <cstddef>

namespace vna::detail {

constexpr std::size_t maximumDriverLayouts = 1024;
constexpr std::size_t maximumDriverWavesPerLayout = 256;
constexpr std::size_t maximumDriverBatchPoints = 1048576;

}  // namespace vna::detail

#endif
