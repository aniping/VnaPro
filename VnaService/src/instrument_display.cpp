/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <utility>

#include "vna_measurement/measurement_processor.h"
#include "vna_service/instrument_service.h"
#include "vna_service/service_error_code.h"

namespace VnaService {
namespace {
OperationResult DisplayResult(const VnaDisplay::DisplayResult &display)
{
    OperationResult result;
    result.ok = display.ok;
    result.errorCode = display.ok ? 0 : Detail::DATA_OUT_OF_RANGE_ERROR;
    result.message = display.error;
    return result;
}

std::string NextMeasurementName(const std::vector<Measurement> &catalog)
{
    // 遵循 ZNB 的自动命名：使用当前目录中最小的空闲正整数编号。
    for (std::size_t index = 1U; index <= catalog.size() + 1U; ++index) {
        const auto name = "Trc" + std::to_string(index);
        const auto existing = std::find_if(catalog.begin(), catalog.end(), [&name](const Measurement &item) {
            return item.name == name;
        });
        if (existing == catalog.end()) {
            return name;
        }
    }
    return {};
}
}  // 匿名命名空间

const VnaDisplay::DisplayState &InstrumentService::Display() const noexcept
{
    return display_.State();
}

bool InstrumentService::PrepareDisplayConfiguration(Detail::ServiceTask &task)
{
    // 显示候选预先验证；核心成功后以无失败的移动同时提交目录与显示。
    task.displayCandidate = display_;
    auto result = task.displayCandidate.Reconcile(task.candidate.measurements);
    if (result.ok) {
        result = task.displayCandidate.ReconcileAxis(
            task.plan.frequencyHz, task.plan.frequencyHz == activePlan_.frequencyHz);
    }
    if (result.ok && task.selectClonedMeasurement) {
        if (task.expectedDisplayRevision != display_.State().revision) {
            result.ok = false;
            result.error = "Display revision changed";
        } else {
            result = task.displayCandidate.ClonePresentation(
                task.cloneSourceName, task.candidate.measurements.back().name, task.candidate.measurements);
        }
    }
    if (!result.ok) {
        auto failure = DisplayResult(result);
        failure.errorCode = Detail::SETTINGS_CONFLICT_ERROR;
        Finish(std::move(failure));
        return false;
    }
    return true;
}

VnaDisplay::FrameContext InstrumentService::DisplayFrame(bool reachedBarrier) const
{
    const auto view = core_.ReadView();
    VnaDisplay::FrameContext result;
    result.frame = view.frame;
    result.acquisitionRevision = settings_.acquisitionRevision;
    result.runId = view.runId;
    result.frequencyHz = &activePlan_.frequencyHz;
    // 当前 DISPLAY 已到队首，排在其后的有限采集不能反向阻止本次读旧完整帧。
    result.current = mockReady_ && (reachedBarrier || !HasMeasurement()) && view.frame &&
                     view.state != VnaCore::CoreState::FAULTED && view.state != VnaCore::CoreState::CLOSED &&
                     view.configRevision == settings_.acquisitionRevision &&
                     view.frame->configRevision == settings_.acquisitionRevision && view.frame->runId == view.runId;
    return result;
}

Operation InstrumentService::ApplyDisplay(
    VnaDisplay::DisplayChange change, std::uint64_t expectedDisplayRevision, const std::string &owner)
{
    Detail::ServiceTask task;
    task.kind = Detail::ServiceTaskKind::DISPLAY;
    task.displayChange = std::move(change);
    task.expectedDisplayRevision = expectedDisplayRevision;
    task.checkDisplayRevision = expectedDisplayRevision != 0U || owner != "SCPI";
    return Enqueue(std::move(task), owner);
}

void InstrumentService::PollDisplay(Detail::ServiceTask &task)
{
    if (task.checkDisplayRevision && task.expectedDisplayRevision != display_.State().revision) {
        OperationResult failure;
        failure.errorCode = Detail::SETTINGS_CONFLICT_ERROR;
        failure.message = "Display revision changed";
        Finish(std::move(failure));
        return;
    }
    Finish(DisplayResult(display_.Apply(task.displayChange, settings_.measurements, DisplayFrame(true))));
}

VnaDisplay::ProjectedTrace InstrumentService::DisplayData(const std::string &name) const
{
    VnaDisplay::ProjectedTrace result;
    const auto context = DisplayFrame();
    if (!context.current) {
        result.error = "No complete measurement for the current configuration";
        return result;
    }
    const auto &state = display_.State();
    const auto selected = name.empty() ? state.selectedMeasurement : name;
    const auto measurement = std::find_if(
        settings_.measurements.begin(), settings_.measurements.end(), [&selected](const Measurement &item) {
            return item.name == selected;
        });
    const auto presentation =
        std::find_if(state.traces.begin(), state.traces.end(), [&selected](const VnaDisplay::TracePresentation &item) {
            return item.name == selected;
        });
    if (measurement == settings_.measurements.end() || presentation == state.traces.end()) {
        result.error = "No measurement selected";
        return result;
    }
    const auto trace = VnaMeasurement::MeasurementProcessor::Calculate(*context.frame, *measurement);
    return VnaDisplay::TraceProjection::Calculate(
        *presentation, trace, context.frame->plan.frequencyHz, *measurement, context.acquisitionRevision);
}

Operation InstrumentService::CloneMeasurement(const std::string &sourceName, std::uint64_t expectedRevision,
    std::uint64_t expectedDisplayRevision, const std::string &owner)
{
    const auto source = sourceName.empty() ? display_.State().selectedMeasurement : sourceName;
    auto operation = ConfigureMeasurements(
        [source](InstrumentSettings &candidate) {
            OperationResult result;
            const auto measurement = std::find_if(
                candidate.measurements.begin(), candidate.measurements.end(), [&source](const Measurement &item) {
                    return item.name == source;
                });
            if (measurement == candidate.measurements.end() ||
                candidate.measurements.size() >= VnaDisplay::MAX_DISPLAY_TRACES) {
                result.errorCode = Detail::INVALID_PARAMETER_ERROR;
                result.message = "Clone source is missing or measurement catalog is full";
                return result;
            }
            auto cloned = *measurement;
            cloned.name = NextMeasurementName(candidate.measurements);
            candidate.measurements.push_back(std::move(cloned));
            result.ok = true;
            return result;
        },
        expectedRevision, owner);
    if (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        auto &task = tasks_.back();
        task.selectClonedMeasurement = true;
        task.cloneSourceName = source;
        task.expectedDisplayRevision = expectedDisplayRevision;
    }
    return operation;
}

}  // 命名空间 VnaService
