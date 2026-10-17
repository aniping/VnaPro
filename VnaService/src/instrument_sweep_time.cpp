/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <utility>

#include "vna_core/acquisition_core.h"
#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace {
bool InterruptibleTask(const Detail::ServiceTask &task)
{
    return task.measurement && !task.earlyStopIssued && task.kind != Detail::ServiceTaskKind::CALIBRATION &&
           (task.stage == Detail::ServiceTaskStage::WAITING_SINGLE_FRAME ||
               task.stage == Detail::ServiceTaskStage::WAITING_TRIGGER);
}

bool SameAcquisition(const Detail::ServiceTask &task, const VnaCore::CoreView &view)
{
    return view.manualSweepTimeSupported && view.state == VnaCore::CoreState::RUNNING &&
           view.runId == task.sweepRunId && view.configRevision == task.sweepConfigRevision;
}

bool MatchingStop(const Detail::ServiceTask &stop, const Detail::ServiceTask &active, const std::string &owner,
    std::uint64_t generation)
{
    return stop.wakeManualSweep && stop.channelId == active.channelId && stop.owner == owner &&
           stop.generation == generation;
}
}  // 匿名命名空间

void InstrumentService::WakeAcceptedStop()
{
    if (tasks_.size() < 2U) {
        return;
    }
    auto &active = tasks_.front();
    if (!InterruptibleTask(active)) {
        return;
    }
    const auto view = core_.ReadView(active.channelId);
    if (!SameAcquisition(active, view) || Settings(active.channelId).sweepTimeAuto) {
        return;
    }
    for (auto &stop : tasks_) {
        if (MatchingStop(stop, active, owner_, controlGeneration_)) {
            // 只提前唤醒已受理 Stop 的软件等待；正式配置与停止提交仍按 FIFO 执行。
            stop.earlyStopTicket = core_.Stop(active.sweepRunId, active.channelId);
            active.earlyStopIssued = true;
            return;
        }
    }
}

bool InstrumentService::ConsumeEarlySweepStop(Detail::ServiceTask &task)
{
    if (!task.earlyStopTicket.valid()) {
        return true;
    }
    if (task.earlyStopTicket.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    const auto stopped = task.earlyStopTicket.get();
    task.earlyStopTicket = VnaCore::Ticket();
    if (!stopped.ok) {
        OperationResult failure;
        failure.errorCode = Detail::DEVICE_SPECIFIC_ERROR;
        failure.message = stopped.message;
        Finish(std::move(failure));
        return false;
    }
    return true;
}
}  // 命名空间 VnaService
