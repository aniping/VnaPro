/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_COMMAND_H
#define VNAPRO_CORE_COMMAND_H

#include <cstdint>
#include <future>

#include "vna_core/core_types.h"
#include "vna_core/sweep_plan.h"

namespace VnaCore {
namespace Detail {
enum class CoreCommandKind {
    INITIALIZE,
    CONFIGURE,
    START,
    TRIGGER,
    STOP,
    RECOVER,
    CLOSE
};

struct CoreCommand final {
    CoreCommandKind kind = CoreCommandKind::INITIALIZE;
    SweepPlan plan;
    RunMode mode = RunMode::MANUAL;
    std::uint64_t runId = 0U;
    std::uint64_t revision = 0U;
    std::promise<CommandResult> promise;
};

}  // namespace Detail
}  // namespace VnaCore

#endif
