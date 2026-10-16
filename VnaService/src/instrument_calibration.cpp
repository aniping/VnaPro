/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace {
bool Ready(const VnaCore::Ticket &ticket)
{
    return ticket.valid() && ticket.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool RequiresHold(CalibrationChangeKind kind) noexcept
{
    return kind == CalibrationChangeKind::BEGIN || kind == CalibrationChangeKind::CAPTURE ||
           kind == CalibrationChangeKind::FINISH || kind == CalibrationChangeKind::CANCEL;
}

OperationResult CalibrationResult(bool ok, int code = 0, const std::string &message = {})
{
    OperationResult result;
    result.ok = ok;
    result.errorCode = code;
    result.message = message;
    return result;
}
}  // 匿名命名空间

bool InstrumentService::RestoreCalibration(
    std::vector<CalibrationSetPtr> sets, std::uint64_t selectedSetId, std::string &error, unsigned channelId)
{
    ContextScope scope(*this, channelId);
    if (!scope.Valid() || !profileReady_ || !owner_.empty() || !tasks_.empty()) {
        error = "Calibration restore requires a ready service without a controller or pending operations";
        return false;
    }
    auto candidate = calibration_;
    if (!candidate.RestoreSaved(std::move(sets), selectedSetId, profile_, error)) {
        return false;
    }
    std::size_t bytes = candidate.StorageBytes();
    for (const auto &channel : channels_) {
        if (channel.first != contextChannelId_) {
            bytes += channel.second.calibration.StorageBytes();
        }
    }
    if (bytes > MAX_INSTRUMENT_CALIBRATION_STORAGE_BYTES) {
        error = "Instrument calibration pool exceeds 1 GiB";
        return false;
    }
    calibration_ = std::move(candidate);
    return true;
}

void InstrumentService::SetCalibrationSaveHandler(CalibrationSaveHandler handler)
{
    if (!owner_.empty() || !tasks_.empty()) {
        throw std::logic_error("Calibration persistence must be installed before accepting a controller");
    }
    calibrationSave_ = std::move(handler);
}

Operation InstrumentService::ApplyCalibration(
    CalibrationChange change, std::uint64_t expectedCalibrationRevision, const std::string &owner, unsigned channelId)
{
    ContextScope channelScope(*this, channelId);
    if (!channelScope.Valid()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "Unknown channel or trace/channel mismatch", owner);
    }
    if (change.kind == CalibrationChangeKind::CAPTURE && HasMeasurement()) {
        return Rejected(Detail::SETTINGS_CONFLICT_ERROR, "A finite acquisition is already pending", owner);
    }
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::CALIBRATION;
    task.measurement = RequiresHold(change.kind);
    task.calibrationChange = std::move(change);
    task.expectedRevision = expectedCalibrationRevision;
    task.checkRevision = expectedCalibrationRevision != 0U || owner != "SCPI";
    return Enqueue(std::move(task), owner);
}

CalibrationSnapshot InstrumentService::Calibration(unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId);
    if (!channelScope.Valid()) {
        CalibrationSnapshot failure;
        failure.reason = "Unknown channel or trace/channel mismatch";
        return failure;
    }
    auto result = calibration_.Snapshot(Identity(), activePlan_);
    const auto view = core_.ReadView(contextChannelId_);
    if (!profileReady_ || view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED ||
        view.configRevision != settings_.acquisitionRevision) {
        result.compatible = false;
        result.reason = "No valid committed acquisition configuration";
    }
    return result;
}

CalibrationSetPtr InstrumentService::FindCalibrationSet(std::uint64_t id, unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId);
    if (!channelScope.Valid()) {
        return {};
    }
    return calibration_.Find(id);
}

CalibrationPreviewResult InstrumentService::CalibrationPreview(
    std::uint64_t sessionId, const std::string &stepId, unsigned channelId) const
{
    ContextScope channelScope(const_cast<InstrumentService &>(*this), channelId);
    if (!channelScope.Valid()) {
        CalibrationPreviewResult failure;
        failure.error = "Unknown channel or trace/channel mismatch";
        return failure;
    }
    return calibration_.Preview(sessionId, stepId);
}

void InstrumentService::FailCalibration(const std::string &error, int code)
{
    const auto &task = tasks_.front();
    if (task.calibrationContextValidated && calibration_.SessionMatches(task.calibrationChange.sessionId) &&
        (task.calibrationChange.kind == CalibrationChangeKind::CAPTURE ||
            task.calibrationChange.kind == CalibrationChangeKind::FINISH)) {
        calibration_.CaptureFailed(error);
    }
    Finish(CalibrationResult(false, code, error));
}

bool InstrumentService::PrepareCalibration(Detail::ServiceTask &task)
{
    if (CalibrationCollecting() && !calibration_.Collecting()) {
        FailCalibration(
            "Calibration collection owns the instrument in another channel", Detail::SETTINGS_CONFLICT_ERROR);
        return false;
    }
    const auto &change = task.calibrationChange;
    const auto revision = calibration_.Revision();
    const bool cancelOldRevision = change.kind == CalibrationChangeKind::CANCEL &&
                                   calibration_.SessionMatches(change.sessionId) && task.expectedRevision <= revision;
    if (task.checkRevision && task.expectedRevision != revision && !cancelOldRevision) {
        FailCalibration("Calibration revision changed", Detail::SETTINGS_CONFLICT_ERROR);
        return false;
    }
    const auto view = core_.ReadView(contextChannelId_);
    if (change.kind == CalibrationChangeKind::CANCEL &&
        (view.state == VnaCore::CoreState::FAULTED || view.state == VnaCore::CoreState::CLOSED)) {
        std::string error;
        auto candidate = calibration_;
        if (!candidate.Cancel(change.sessionId, error)) {
            FailCalibration(error, Detail::SETTINGS_CONFLICT_ERROR);
            return false;
        }
        calibration_ = std::move(candidate);
        calibrationRun_ = {};
        settings_.sweepMode = SweepMode::HOLD;
        Finish(CalibrationResult(true));
        return false;
    }
    if ((view.state != VnaCore::CoreState::READY && view.state != VnaCore::CoreState::RUNNING) ||
        view.configRevision != settings_.acquisitionRevision) {
        FailCalibration("Calibration requires the current committed acquisition plan", Detail::SETTINGS_CONFLICT_ERROR);
        return false;
    }
    std::string error;
    if (change.kind == CalibrationChangeKind::FINISH || change.kind == CalibrationChangeKind::CANCEL) {
        if (calibrationRun_.sessionId != change.sessionId || !calibration_.SessionMatches(change.sessionId)) {
            FailCalibration(
                "Calibration session has no matching entry acquisition state", Detail::SETTINGS_CONFLICT_ERROR);
            return false;
        }
        task.source = calibrationRun_.source;
        task.mode = calibrationRun_.mode;
    }
    // 采集只在成功后提交该标准，避免每步深复制全部既有标准数组。
    if (change.kind != CalibrationChangeKind::CAPTURE) {
        task.calibrationCandidate = calibration_;
    }
    if (change.kind == CalibrationChangeKind::CAPTURE) {
        if (!calibration_.ValidateStep(change.sessionId, change.stepId, error) ||
            !calibration_.SessionCompatible(Identity(), activePlan_)) {
            FailCalibration(
                error.empty() ? "Calibration session stimulus changed" : error, Detail::SETTINGS_CONFLICT_ERROR);
            return false;
        }
        task.calibrationContextValidated = true;
    } else if (change.kind == CalibrationChangeKind::BEGIN) {
        if (!task.calibrationCandidate.Begin(change, Identity(), activePlan_, profile_, error)) {
            FailCalibration(error, Detail::SETTINGS_CONFLICT_ERROR);
            return false;
        }
        task.source = settings_.triggerSource;
        task.mode = settings_.sweepMode;
    } else if (change.kind == CalibrationChangeKind::FINISH) {
        if (!calibration_.SessionMatches(change.sessionId) ||
            !calibration_.SessionCompatible(Identity(), activePlan_)) {
            FailCalibration("Calibration session is missing or its stimulus changed", Detail::SETTINGS_CONFLICT_ERROR);
            return false;
        }
        task.calibrationContextValidated = true;
        if (!task.calibrationCandidate.Complete(change.name, error)) {
            FailCalibration(error, Detail::DATA_OUT_OF_RANGE_ERROR);
            return false;
        }
    } else if (change.kind == CalibrationChangeKind::CANCEL) {
        if (!task.calibrationCandidate.Cancel(change.sessionId, error)) {
            FailCalibration(error, Detail::SETTINGS_CONFLICT_ERROR);
            return false;
        }
    } else if (!task.calibrationCandidate.ApplySaved(change, profile_, error)) {
        FailCalibration(error, Detail::DATA_OUT_OF_RANGE_ERROR);
        return false;
    }
    if (change.kind == CalibrationChangeKind::ACTIVATE &&
        !task.calibrationCandidate.Snapshot(Identity(), activePlan_).compatible) {
        FailCalibration(
            "Calibration set does not match the current stimulus or device", Detail::SETTINGS_CONFLICT_ERROR);
        return false;
    }
    std::size_t calibrationBytes = task.calibrationCandidate.StorageBytes();
    for (const auto &channel : channels_) {
        if (channel.first != contextChannelId_) {
            calibrationBytes += channel.second.calibration.StorageBytes();
        }
    }
    if (change.kind != CalibrationChangeKind::CAPTURE && calibrationBytes > MAX_INSTRUMENT_CALIBRATION_STORAGE_BYTES) {
        FailCalibration("Instrument calibration pool exceeds 1 GiB", Detail::DATA_OUT_OF_RANGE_ERROR);
        return false;
    }
    task.plan = activePlan_;
    if (change.kind == CalibrationChangeKind::CAPTURE) {
        calibration_.ExtendPlan(task.plan);
    } else {
        task.calibrationCandidate.ExtendPlan(task.plan);
    }
    if (change.kind == CalibrationChangeKind::CAPTURE) {
        calibration_.SetCapturePending(true);
    }
    if (RequiresHold(change.kind) && view.state == VnaCore::CoreState::RUNNING) {
        task.ticket = core_.Stop(view.runId, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_STOP;
    } else {
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
        ContinueCalibration(task);
    }
    return true;
}

void InstrumentService::ContinueCalibration(Detail::ServiceTask &task)
{
    if (task.plan.receiverMask != activePlan_.receiverMask || task.plan.sourcePorts != activePlan_.sourcePorts) {
        task.ticket = core_.Configure(task.plan, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_CONFIGURATION;
        return;
    }
    if (task.calibrationChange.kind == CalibrationChangeKind::CAPTURE) {
        task.ticket = core_.Start(VnaCore::RunMode::MANUAL, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_START;
        return;
    }
    if (!BeginCalibrationSave(task)) {
        return;
    }
    if (RequiresHold(task.calibrationChange.kind)) {
        settings_.sweepMode = SweepMode::HOLD;
    }
    calibration_ = std::move(task.calibrationCandidate);
    const auto kind = task.calibrationChange.kind;
    if (kind == CalibrationChangeKind::BEGIN) {
        calibrationRun_ = {calibration_.Snapshot(Identity(), activePlan_).sessionId, task.source, task.mode};
        settings_.triggerSource = TriggerSource::MANUAL;
    } else if (kind == CalibrationChangeKind::FINISH || kind == CalibrationChangeKind::CANCEL) {
        // 有效系数先发布；恢复失败仍保留校准集，但整个操作不能报告成功。
        calibrationRun_ = {};
        task.candidate = settings_;
        task.candidate.triggerSource = task.source;
        task.candidate.sweepMode = task.mode;
        task.calibrationResuming = true;
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
        PollMode(task);
        return;
    }
    Finish(CalibrationResult(true));
}

bool InstrumentService::BeginCalibrationSave(Detail::ServiceTask &task)
{
    const auto kind = task.calibrationChange.kind;
    if (!calibrationSave_ || task.calibrationSaved || kind == CalibrationChangeKind::BEGIN ||
        kind == CalibrationChangeKind::CANCEL || kind == CalibrationChangeKind::ENABLE) {
        return true;
    }
    try {
        task.calibrationSave = calibrationSave_(
            contextChannelId_, task.calibrationCandidate.SavedSets(), task.calibrationCandidate.SelectedSetId());
        if (!task.calibrationSave.valid()) {
            FailCalibration("Calibration persistence returned an invalid completion", Detail::DEVICE_SPECIFIC_ERROR);
            return false;
        }
        task.stage = Detail::ServiceTaskStage::WAITING_CALIBRATION_SAVE;
    } catch (const std::exception &error) {
        FailCalibration(std::string("Calibration persistence failed: ") + error.what(), Detail::DEVICE_SPECIFIC_ERROR);
    }
    return false;
}

void InstrumentService::PollCalibration(Detail::ServiceTask &task)
{
    if (task.stage == Detail::ServiceTaskStage::WAITING_CALIBRATION_SAVE) {
        if (task.calibrationSave.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        try {
            const auto result = task.calibrationSave.get();
            if (!result.ok) {
                FailCalibration("Calibration persistence failed: " + result.message, Detail::DEVICE_SPECIFIC_ERROR);
                return;
            }
        } catch (const std::exception &error) {
            FailCalibration(
                std::string("Calibration persistence failed: ") + error.what(), Detail::DEVICE_SPECIFIC_ERROR);
            return;
        }
        task.calibrationSave = {};
        task.calibrationSaved = true;
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
        ContinueCalibration(task);
        return;
    }
    if (task.calibrationResuming) {
        PollMode(task);
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::PENDING) {
        PrepareCalibration(task);
        return;
    }
    if (!Ready(task.ticket)) {
        return;
    }
    const auto result = task.ticket.get();
    task.ticket = {};
    if (!result.ok) {
        FailCalibration(result.message, Detail::DEVICE_SPECIFIC_ERROR);
        return;
    }
    if (task.stage == Detail::ServiceTaskStage::WAITING_STOP) {
        settings_.sweepMode = SweepMode::HOLD;
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
        ContinueCalibration(task);
    } else if (task.stage == Detail::ServiceTaskStage::WAITING_CONFIGURATION) {
        settings_.acquisitionRevision = result.configRevision;
        activePlan_ = task.plan;
        const auto names = ChannelTraceNames(contextChannelId_);
        display_.BindAcquisitionRevision(settings_.acquisitionRevision, &names);
        task.stage = Detail::ServiceTaskStage::READY_TO_START;
        ContinueCalibration(task);
    } else if (task.stage == Detail::ServiceTaskStage::WAITING_START) {
        task.captureRunId = result.runId;
        task.captureConfigRevision = result.configRevision;
        task.ticket = core_.TriggerOnce(result.runId, result.configRevision, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_SINGLE_FRAME;
    } else if (task.stage == Detail::ServiceTaskStage::WAITING_SINGLE_FRAME) {
        task.captureFrameSeq = result.frameSeq;
        const auto view = core_.ReadView(contextChannelId_);
        task.capturedFrame = view.frame;
        // TriggerOnce 返回的身份必须对应完整的新运行，不接受被保留的旧标准或 DUT。
        if (!task.capturedFrame || result.runId != task.captureRunId ||
            result.configRevision != task.captureConfigRevision || result.frameSeq == 0U ||
            task.capturedFrame->runId != result.runId || task.capturedFrame->configRevision != result.configRevision ||
            task.capturedFrame->frameSeq != result.frameSeq) {
            // 本阶段仍必须经过停止屏障，错误在屏障后统一报告。
            task.capturedFrame.reset();
        }
        task.ticket = core_.Stop(task.captureRunId, contextChannelId_);
        task.stage = Detail::ServiceTaskStage::WAITING_FINAL_STOP;
    } else if (task.stage == Detail::ServiceTaskStage::WAITING_FINAL_STOP) {
        settings_.sweepMode = SweepMode::HOLD;
        const auto view = core_.ReadView(contextChannelId_);
        if (!task.capturedFrame || view.runId != task.captureRunId ||
            view.configRevision != task.captureConfigRevision ||
            !calibration_.SessionMatches(task.calibrationChange.sessionId) ||
            !calibration_.SessionCompatible(Identity(), activePlan_)) {
            FailCalibration(
                "No complete new standard frame for the current calibration session", Detail::SETTINGS_CONFLICT_ERROR);
            return;
        }
        std::string error;
        if (!calibration_.Capture(task.calibrationChange.stepId, *task.capturedFrame, error)) {
            FailCalibration(error, Detail::DATA_OUT_OF_RANGE_ERROR);
            return;
        }
        Finish(CalibrationResult(true));
    }
}

}  // 命名空间 VnaService
