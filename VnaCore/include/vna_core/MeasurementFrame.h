#ifndef VNAPRO_VNA_CORE_MEASUREMENT_FRAME_H
#define VNAPRO_VNA_CORE_MEASUREMENT_FRAME_H

#include <cstdint>
#include <memory>
#include <vector>

#include "vna_core/SweepPlan.h"

namespace vna_core {

struct PortFrame {
    std::uint32_t sourcePort = 0U;
    std::vector<double> iq;  // Point-major: A.real,A.imag,B.real,B.imag for each receiver.
};

struct MeasurementFrame {
    std::uint32_t channelId = 1U;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    std::uint64_t frameSeq = 0U;
    SweepPlan plan;
    std::vector<PortFrame> ports;
};

using FramePtr = std::shared_ptr<const MeasurementFrame>;

}  // namespace vna_core

#endif
