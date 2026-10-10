#ifndef VNAPRO_VNACORE_INCLUDE_VNA_CORE_PORTFRAME_H
#define VNAPRO_VNACORE_INCLUDE_VNA_CORE_PORTFRAME_H
#include <cstdint>
#include <vector>

namespace vna_core {

struct PortFrame {
    std::uint32_t sourcePort = 0U;
    std::vector<double> iq;  // Point-major: A.real,A.imag,B.real,B.imag for each receiver.
};

}  // namespace vna_core

#endif
