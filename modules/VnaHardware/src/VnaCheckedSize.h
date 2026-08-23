#ifndef VNA_HARDWARE_VNA_CHECKED_SIZE_H
#define VNA_HARDWARE_VNA_CHECKED_SIZE_H

#include <cstddef>

namespace vna::detail {

bool checkedSizeAdd(std::size_t left, std::size_t right, std::size_t &result) noexcept;
bool checkedSizeMultiply(std::size_t left, std::size_t right, std::size_t &result) noexcept;

}  // namespace vna::detail

#endif
