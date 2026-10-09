#ifndef VNAPRO_VNA_CORE_CORE_TYPES_H
#define VNAPRO_VNA_CORE_CORE_TYPES_H

#include <cstdint>
#include <future>
#include <string>

#include "vna_core/MeasurementFrame.h"

namespace vna_core {

enum class CoreState { Uninitialized, Ready, Running, Faulted, Closed };
enum class RunMode { Manual, Continuous };

struct CommandResult {
    bool ok = false;
    std::string message;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    std::uint64_t frameSeq = 0U;
};

using Ticket = std::shared_future<CommandResult>;

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
