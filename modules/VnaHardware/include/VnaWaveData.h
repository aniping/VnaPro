#ifndef VNA_HARDWARE_VNA_WAVE_DATA_H
#define VNA_HARDWARE_VNA_WAVE_DATA_H

#include <cstdint>

namespace vna {

// Valid 为零，其余值是可组合的位标志；Missing 表示该样本不可用。
enum class WaveQuality : std::uint32_t {
    Valid = 0,
    Overload = 1,
    Unlocked = 2,
    SourceUnsettled = 4,
    ReferenceLow = 8,
    Missing = 16
};

}  // namespace vna

#endif
