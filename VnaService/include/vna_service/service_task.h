/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVICE_TASK_H
#define VNAPRO_SERVICE_TASK_H

#include <functional>
#include <future>
#include <string>

#include "vna_core/core_types.h"
#include "vna_core/sweep_plan.h"
#include "vna_display/display_workspace.h"
#include "vna_service/instrument_settings.h"
#include "vna_service/operation_result.h"

namespace VnaService {
namespace Detail {
enum class ServiceTaskKind {
    CONFIGURE,
    MEASUREMENTS,
    DISPLAY,
    SOURCE,
    MODE,
    TRIGGER
};

enum class ServiceTaskStage {
    PENDING,
    WAITING_CONFIGURATION,
    WAITING_TRIGGER,
    WAITING_STOP,
    READY_TO_START,
    WAITING_START,
    WAITING_SINGLE_FRAME,
    WAITING_FINAL_STOP
};

struct ServiceTask final {
    ServiceTaskKind kind = ServiceTaskKind::CONFIGURE;
    ServiceTaskStage stage = ServiceTaskStage::PENDING;
    bool measurement = false;
    bool forceInternalSource = false;
    std::string owner = "SCPI";
    std::uint64_t generation = 0;
    TriggerSource source = TriggerSource::IMMEDIATE;
    SweepMode mode = SweepMode::HOLD;
    std::uint64_t expectedRevision = 0;
    bool checkRevision = false;
    std::function<OperationResult(InstrumentSettings &)> mutate;
    InstrumentSettings candidate;
    VnaDisplay::DisplayWorkspace displayCandidate;
    VnaDisplay::DisplayChange displayChange;
    bool selectClonedMeasurement = false;
    bool selectNewMeasurement = false;
    bool newDiagram = false;
    unsigned targetDiagram = 0U;
    unsigned deletedDiagram = 0U;
    // 测量定义与显示设置使用同一个源名称，不由客户端单独复制显示属性。
    std::string cloneSourceName;
    std::uint64_t expectedDisplayRevision = 0U;
    bool checkDisplayRevision = false;
    VnaCore::SweepPlan plan;
    VnaCore::Ticket ticket;
    std::promise<OperationResult> promise;
    Operation operation;

    ServiceTask(const ServiceTask &) = delete;
    ServiceTask &operator=(const ServiceTask &) = delete;
    ServiceTask(ServiceTask &&) noexcept = default;
    ServiceTask &operator=(ServiceTask &&) noexcept = default;

    // 任务进入先进先出队列前，先创建对应的 future。
    ServiceTask() : operation(promise.get_future().share())
    {
    }
};
}  // 命名空间 Detail
}  // 命名空间 VnaService

#endif
