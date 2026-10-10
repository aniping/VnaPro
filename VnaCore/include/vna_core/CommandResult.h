#ifndef VNAPRO_VNACORE_INCLUDE_VNA_CORE_COMMANDRESULT_H
#define VNAPRO_VNACORE_INCLUDE_VNA_CORE_COMMANDRESULT_H
#include <cstdint>
#include <future>
#include <string>

namespace vna_core {

struct CommandResult {
    bool ok = false;
    std::string message;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    std::uint64_t frameSeq = 0U;
};

using Ticket = std::shared_future<CommandResult>;

}  // namespace vna_core

#endif
