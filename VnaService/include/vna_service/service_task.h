/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVICE_TASK_H
#define VNAPRO_SERVICE_TASK_H

#include <deque>
#include <functional>
#include <future>
#include <string>

#include "vna_core/core_types.h"
#include "vna_core/sweep_plan.h"
#include "vna_display/display_workspace.h"
#include "vna_service/calibration_workspace.h"
#include "vna_service/instrument_settings.h"
#include "vna_service/operation_result.h"
#include "vna_service/stimulus_zoom.h"

namespace VnaService {
namespace Detail {
enum class ServiceTaskKind {
    CONFIGURE,
    MEASUREMENTS,
    DISPLAY,
    STIMULUS_ZOOM,
    CHANNEL_CREATE,
    CHANNEL_SELECT,
    CHANNEL_DELETE,
    SOURCE,
    MODE,
    SELECT_SOURCE,
    SELECT_MODE,
    START,
    TRIGGER,
    CALIBRATION
};

enum class ServiceTaskStage {
    PENDING,
    WAITING_CONFIGURATION,
    WAITING_TRIGGER,
    WAITING_STOP,
    READY_TO_START,
    WAITING_START,
    WAITING_SINGLE_FRAME,
    WAITING_FINAL_STOP,
    WAITING_CHANNEL_CREATE,
    WAITING_CHANNEL_START,
    WAITING_CHANNEL_REMOVE
};

// 只保存 Zoom 修改的字段；撤回不能覆盖后来新增的标记、目录或其他显示设置。
struct StimulusZoomHistoryEntry final {
    bool targetUnchanged = true;
    std::string traceName;
    std::string parameter;
    VnaDisplay::DisplayFormat format = VnaDisplay::DisplayFormat::DB;
    double previousStartHz = 0.0;
    double previousStopHz = 0.0;
    double appliedStartHz = 0.0;
    double appliedStopHz = 0.0;
    VnaDisplay::DisplayScale previousScale;
    VnaDisplay::DisplayScale appliedScale;
};

struct ServiceTask final {
    ServiceTaskKind kind = ServiceTaskKind::CONFIGURE;
    ServiceTaskStage stage = ServiceTaskStage::PENDING;
    bool measurement = false;
    bool forceInternalSource = false;
    std::string owner = "SCPI";
    std::uint64_t generation = 0;
    // 受理时冻结，异步阶段不再从全局显示选择推断写入目标。
    unsigned channelId = 0U;
    unsigned targetChannelId = 0U;
    bool copyChannel = false;
    OperationResult channelFailure;
    std::vector<std::pair<std::string, std::string>> channelTraceCopies;
    std::vector<std::string> channelTraceNames;
    TriggerSource source = TriggerSource::IMMEDIATE;
    SweepMode mode = SweepMode::HOLD;
    std::uint64_t expectedRevision = 0;
    bool checkRevision = false;
    std::function<OperationResult(InstrumentSettings &)> mutate;
    InstrumentSettings candidate;
    VnaDisplay::DisplayWorkspace displayCandidate;
    VnaDisplay::DisplayChange displayChange;
    StimulusZoomChange stimulusZoomChange;
    std::deque<StimulusZoomHistoryEntry> zoomHistoryCandidate;
    bool selectClonedMeasurement = false;
    bool selectNewMeasurement = false;
    bool newDiagram = false;
    VnaDisplay::TraceDropPosition dropPosition = VnaDisplay::TraceDropPosition::CENTER;
    unsigned targetDiagram = 0U;
    unsigned deletedDiagram = 0U;
    // 非零表示整组替换；端口数量取自启动配置，不由客户端覆盖。
    unsigned matrixPortCount = 0U;
    // 测量定义与显示设置使用同一个源名称，不由客户端单独复制显示属性。
    std::string cloneSourceName;
    std::uint64_t expectedDisplayRevision = 0U;
    bool checkDisplayRevision = false;
    VnaCore::SweepPlan plan;
    VnaCore::Ticket ticket;
    std::promise<OperationResult> promise;
    Operation operation;
    CalibrationChange calibrationChange;
    CalibrationWorkspace calibrationCandidate;
    bool calibrationContextValidated = false;
    // 校准工作区已提交后，复用普通模式迁移等待真实启动完成。
    bool calibrationResuming = false;
    std::uint64_t captureRunId = 0U;
    std::uint64_t captureConfigRevision = 0U;
    std::uint64_t captureFrameSeq = 0U;
    VnaCore::FramePtr capturedFrame;

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
