/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_CORE_TYPES_H
#define VNAPRO_VNA_CORE_CORE_TYPES_H

#include <cstdint>
#include <future>
#include <string>

#include "vna_core/measurement_frame.h"

namespace vna_core {

enum class CoreState {
    UNINITIALIZED,
    READY,
    RUNNING,
    FAULTED,
    CLOSED
};

enum class RunMode {
    MANUAL,
    CONTINUOUS
};

struct CommandResult final {
    bool ok = false;
    std::string message;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    std::uint64_t frameSeq = 0U;
};

using Ticket = std::shared_future<CommandResult>;

struct CoreView final {
    CoreState state = CoreState::UNINITIALIZED;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    FramePtr frame;
    std::string error;
    std::string deviceIdn;
};

}  // namespace vna_core

#endif
