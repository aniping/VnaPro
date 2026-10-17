/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_CORE_TYPES_H
#define VNAPRO_VNA_CORE_CORE_TYPES_H

#include <cstdint>
#include <future>
#include <string>

#include "vna_core/measurement_frame.h"
#include "vna_core/sweep_progress.h"

namespace VnaCore {

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
    unsigned channelId = 1U;
};

using Ticket = std::shared_future<CommandResult>;

struct CoreView final {
    CoreState state = CoreState::UNINITIALIZED;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    FramePtr frame;
    SweepProgressPtr sweepProgress;
    bool manualSweepTimeSupported = false;
    // 当前配置最近完整扫描的主机回读耗时；0 表示尚未完成或配置已改变。
    double lastSweepTimeSeconds = 0.0;
    std::string error;
    std::string deviceIdn;
    unsigned channelId = 1U;
};

}  // 命名空间 VnaCore

#endif
