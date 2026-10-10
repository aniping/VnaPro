#ifndef VNAPRO_VNACORE_INCLUDE_VNA_CORE_COREVIEW_H
#define VNAPRO_VNACORE_INCLUDE_VNA_CORE_COREVIEW_H
#include <cstdint>
#include <string>

#include "vna_core/CoreState.h"
#include "vna_core/MeasurementFrame.h"

namespace vna_core {

struct CoreView {
    CoreState state = CoreState::Uninitialized;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    FramePtr frame;
    std::string error;
    std::string deviceIdn;
};

}  // namespace vna_core

#endif
