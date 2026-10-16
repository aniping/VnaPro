/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"

namespace {
using VnaDisplay::DisplayChange;
using VnaDisplay::DisplayChangeKind;
using VnaService::StimulusZoomChange;
using VnaServiceTest::Fixture;
const std::string OWNER = "WEB";

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Wait(const std::function<bool()> &condition, const std::string &message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        Require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    Wait(
        [&] {
            fixture.service.Poll();
            return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        },
        "等待 Stimulus Zoom 操作超时");
    return operation.get();
}

void Prepare(Fixture &fixture)
{
    Require(fixture.service.TryAcquireControl(OWNER), "取得 Zoom 控制权");
    VnaService::ConfigPatch points;
    points.hasPoints = true;
    points.points = 5U;
    Require(Finish(fixture, fixture.service.ApplyConfiguration(points, fixture.service.Settings().revision, OWNER)).ok,
        "准备五点扫描");
}

StimulusZoomChange Selection(const Fixture &fixture)
{
    const auto snapshot = fixture.service.Snapshot();
    StimulusZoomChange change;
    change.traceName = snapshot.display.selectedMeasurement;
    const double span = snapshot.settings.stopHz - snapshot.settings.startHz;
    change.startHz = snapshot.settings.startHz + span / 4.0;
    change.stopHz = snapshot.settings.stopHz - span / 4.0;
    const auto &scale = snapshot.display.traces.front().scale;
    change.min = scale.min + (scale.max - scale.min) / 4.0;
    change.max = scale.max - (scale.max - scale.min) / 4.0;
    return change;
}

VnaService::Operation Submit(Fixture &fixture, const StimulusZoomChange &change)
{
    const auto snapshot = fixture.service.Snapshot();
    return fixture.service.ApplyStimulusZoom(change, snapshot.settings.revision, snapshot.display.revision, OWNER);
}

VnaService::Operation Out(Fixture &fixture)
{
    StimulusZoomChange change;
    change.action = VnaService::StimulusZoomAction::OUT;
    return Submit(fixture, change);
}

void Unchanged(const VnaService::InstrumentSnapshot &before, const VnaService::InstrumentSnapshot &after)
{
    Require(before.settings.startHz == after.settings.startHz && before.settings.stopHz == after.settings.stopHz &&
                before.settings.revision == after.settings.revision &&
                before.settings.acquisitionRevision == after.settings.acquisitionRevision &&
                before.display.revision == after.display.revision &&
                before.display.traces.front().scale.min == after.display.traces.front().scale.min &&
                before.display.traces.front().scale.max == after.display.traces.front().scale.max &&
                before.stimulusZoom.depth == after.stimulusZoom.depth,
        "拒绝或失败不能部分提交频率、显示、版本或历史");
}

void HoldZoomCommitsRealFrequenciesAndScaleTogether()
{
    Fixture fixture;
    Prepare(fixture);
    const auto before = fixture.service.Snapshot();
    auto change = Selection(fixture);
    change.startHz += 0.2;
    change.stopHz += 0.7;
    Require(Finish(fixture, Submit(fixture, change)).ok, "HOLD 下执行 Zoom");
    const auto after = fixture.service.Snapshot();
    Require(after.settings.startHz == std::round(change.startHz) &&
                after.settings.stopHz == std::round(change.stopHz) &&
                after.display.traces.front().scale.min == change.min &&
                after.display.traces.front().scale.max == change.max &&
                after.display.traces.front().scale.referencePosition ==
                    before.display.traces.front().scale.referencePosition &&
                after.settings.points == before.settings.points &&
                after.settings.ifBandwidthHz == before.settings.ifBandwidthHz &&
                after.core.state == VnaCore::CoreState::READY && after.core.runId == before.core.runId &&
                !after.core.frame && after.settings.revision == before.settings.revision + 1U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision + 1U &&
                after.display.revision == before.display.revision + 1U && after.stimulusZoom.depth == 1U &&
                after.stimulusZoom.available && after.stimulusZoom.canZoomOut,
        "真实频率按 Hz 舍入并与纵轴一次提交，保持点数、带宽、参考位置和停止状态");
    Require(Finish(fixture, Out(fixture)).ok, "恢复上次 Zoom 前范围");
    const auto restored = fixture.service.Snapshot();
    Require(restored.settings.startHz == before.settings.startHz &&
                restored.settings.stopHz == before.settings.stopHz &&
                restored.display.traces.front().scale.min == before.display.traces.front().scale.min &&
                restored.display.traces.front().scale.max == before.display.traces.front().scale.max &&
                restored.stimulusZoom.depth == 0U && !restored.stimulusZoom.canZoomOut,
        "Zoom Out 恢复频率和纵轴并移出历史");
}

void ContinuousZoomWaitsForCompleteFrame()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.RunContinuous(OWNER)).ok, "启动 Continuous");
    fixture.device->Arm();
    Wait(
        [&] {
            return fixture.device->HasEntered();
        },
        "等待旧连续帧进入关卡");
    const auto before = fixture.service.Snapshot();
    const auto change = Selection(fixture);
    const auto zoom = Submit(fixture, change);
    fixture.service.Poll();
    Require(zoom.wait_for(std::chrono::seconds(0)) != std::future_status::ready, "Zoom 等待完整帧边界");
    Unchanged(before, fixture.service.Snapshot());
    fixture.device->Release();
    Require(Finish(fixture, zoom).ok, "完整帧边界执行原子 Zoom");
    Wait(
        [&] {
            const auto snapshot = fixture.service.Snapshot();
            return snapshot.core.frame && snapshot.core.frame->configRevision == snapshot.settings.acquisitionRevision;
        },
        "等待使用新真实频率轴的完整帧");
    const auto after = fixture.service.Snapshot();
    Require(after.core.runId == before.core.runId && after.core.state == VnaCore::CoreState::RUNNING &&
                after.settings.sweepMode == before.settings.sweepMode &&
                after.settings.selectedSweepMode == before.settings.selectedSweepMode &&
                after.settings.triggerSource == before.settings.triggerSource &&
                after.core.frame->plan.frequencyHz.front() == change.startHz &&
                after.core.frame->plan.frequencyHz.back() == change.stopHz &&
                after.core.frame->plan.frequencyHz.size() == 5U,
        "CONT Zoom 保持运行身份和触发方式，新完整帧具有五点新频率范围");
}

void ManualZoomDoesNotAcquireAndFiniteZoomWaits()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(VnaService::TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.StartSweep(OWNER)).ok,
        "准备 Continuous/Manual 布防");
    const auto run = fixture.core.ReadView().runId;
    Require(Finish(fixture, Submit(fixture, Selection(fixture))).ok && !fixture.core.ReadView().frame &&
                fixture.core.ReadView().runId == run && fixture.core.ReadView().state == VnaCore::CoreState::RUNNING &&
                fixture.service.Settings().triggerSource == VnaService::TriggerSource::MANUAL,
        "Manual Zoom 只更新计划，不触发或重启运行");
    Require(Finish(fixture, fixture.service.RunSelectedSingle(OWNER)).ok, "准备 Single/Manual");
    fixture.device->Arm();
    const auto trigger = fixture.service.Trigger(OWNER);
    Wait(
        [&] {
            fixture.service.Poll();
            return fixture.device->HasEntered();
        },
        "等待有限帧关卡");
    const auto before = fixture.service.Snapshot();
    Require(!before.stimulusZoom.available, "有限采集中报告等待原因");
    const auto zoom = Submit(fixture, Selection(fixture));
    fixture.service.Poll();
    Require(zoom.wait_for(std::chrono::seconds(0)) != std::future_status::ready, "Zoom 不越过已受理有限帧");
    Unchanged(before, fixture.service.Snapshot());
    fixture.device->Release();
    Require(Finish(fixture, trigger).ok && Finish(fixture, zoom).ok &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                fixture.service.Settings().selectedSweepMode == VnaService::SweepMode::SINGLE &&
                fixture.service.Settings().triggerSource == VnaService::TriggerSource::MANUAL,
        "有限帧完成并停止后才提交 Zoom，不改变选中模式和触发源");
}

void VerticalOnlyZoomKeepsManualCompletedFrameCurrent()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(VnaService::TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.StartSweep(OWNER)).ok &&
                Finish(fixture, fixture.service.Trigger(OWNER)).ok,
        "发布 Manual 当前完整帧");
    const auto before = fixture.service.Snapshot();
    auto change = Selection(fixture);
    change.startHz = before.settings.startHz;
    change.stopHz = before.settings.stopHz;
    Require(Finish(fixture, Submit(fixture, change)).ok, "仅收窄纵轴的 Stimulus Zoom");
    const auto zoomed = fixture.service.Snapshot();
    Require(zoomed.core.frame == before.core.frame && zoomed.core.runId == before.core.runId &&
                zoomed.settings.revision == before.settings.revision &&
                zoomed.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                zoomed.core.configRevision == before.core.configRevision &&
                zoomed.display.revision == before.display.revision + 1U &&
                fixture.service.DisplayData("Trc1").available && zoomed.stimulusZoom.depth == 1U,
        "Y-only 不重下计划或触发采集，原帧和配置版本保持 current，仅显示及历史提交");
    Require(Finish(fixture, Out(fixture)).ok, "撤回仅纵轴变化");
    const auto restored = fixture.service.Snapshot();
    Require(restored.core.frame == before.core.frame && restored.core.runId == before.core.runId &&
                restored.settings.revision == before.settings.revision &&
                restored.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                restored.display.traces.front().scale.min == before.display.traces.front().scale.min &&
                restored.display.traces.front().scale.max == before.display.traces.front().scale.max &&
                fixture.service.DisplayData("Trc1").available && restored.stimulusZoom.depth == 0U,
        "Y-only Out 同样保留 Manual 当前完整帧及原物理配置");
}

void DualCasAndDeviceFailurePreserveBothDomains()
{
    Fixture fixture;
    Prepare(fixture);
    const auto before = fixture.service.Snapshot();
    const auto change = Selection(fixture);
    Require(!Finish(fixture, fixture.service.ApplyStimulusZoom(
                                 change, before.settings.revision - 1U, before.display.revision, OWNER))
                    .ok &&
                !Finish(fixture, fixture.service.ApplyStimulusZoom(
                                     change, before.settings.revision, before.display.revision - 1U, OWNER))
                     .ok &&
                !fixture.service.ApplyStimulusZoom(change, before.settings.revision, before.display.revision, "OTHER")
                     .get()
                     .ok,
        "Zoom 要求有效控制权及两个精确版本");
    Unchanged(before, fixture.service.Snapshot());
    fixture.device->FailNextPlanPreparation();
    Require(!Finish(fixture, Submit(fixture, change)).ok, "设备计划准备失败拒绝 Zoom");
    Unchanged(before, fixture.service.Snapshot());
    const auto first = Submit(fixture, change);
    const auto stale = Submit(fixture, change);
    Require(Finish(fixture, first).ok, "提交第一笔 Zoom");
    const auto committed = fixture.service.Snapshot();
    Require(!Finish(fixture, stale).ok, "执行时再次检查双 CAS，排队旧版本不得覆盖新结果");
    Unchanged(committed, fixture.service.Snapshot());
    fixture.device->FailNextPlanPreparation();
    Require(!Finish(fixture, Out(fixture)).ok && fixture.service.StimulusZoom().depth == 1U,
        "Zoom Out 的设备失败保留历史以便重试");
    Unchanged(committed, fixture.service.Snapshot());
}

void InvalidSelectionsAndNoopDoNotChangeState()
{
    Fixture fixture;
    Prepare(fixture);
    const auto before = fixture.service.Snapshot();
    for (unsigned invalid = 0U; invalid < 8U; ++invalid) {
        auto change = Selection(fixture);
        if (invalid == 0U) {
            change.startHz = before.settings.startHz - 10.0;
        }
        if (invalid == 1U) {
            change.stopHz = before.settings.stopHz + 10.0;
        }
        if (invalid == 2U) {
            change.min = before.display.traces.front().scale.min - 1.0;
        }
        if (invalid == 3U) {
            change.max = change.min;
        }
        if (invalid == 4U) {
            change.stopHz = std::numeric_limits<double>::infinity();
        }
        if (invalid == 5U) {
            change.traceName = "missing";
        }
        if (invalid == 6U) {
            change.startHz = before.settings.startHz + 0.1;
            change.stopHz = before.settings.startHz + 0.2;
        }
        if (invalid == 7U) {
            change.action = static_cast<VnaService::StimulusZoomAction>(99);
        }
        Require(!Finish(fixture, Submit(fixture, change)).ok, "非法框选必须拒绝");
        Unchanged(before, fixture.service.Snapshot());
    }
    StimulusZoomChange same;
    same.traceName = before.display.selectedMeasurement;
    same.startHz = before.settings.startHz - 0.2;
    same.stopHz = before.settings.stopHz + 0.2;
    same.min = before.display.traces.front().scale.min;
    same.max = before.display.traces.front().scale.max;
    Require(Finish(fixture, Submit(fixture, same)).ok, "边界内浮点容差 clamp 后同范围幂等成功");
    Unchanged(before, fixture.service.Snapshot());
    DisplayChange hidden;
    hidden.kind = DisplayChangeKind::VISIBLE;
    hidden.visible = false;
    Require(Finish(fixture, fixture.service.ApplyDisplay(hidden, fixture.service.Display().revision, OWNER)).ok,
        "隐藏活动迹线");
    Require(!fixture.service.StimulusZoom().available && !Finish(fixture, Submit(fixture, Selection(fixture))).ok,
        "隐藏迹线不支持框选");
    hidden.visible = true;
    Require(Finish(fixture, fixture.service.ApplyDisplay(hidden, fixture.service.Display().revision, OWNER)).ok,
        "恢复显示");
    DisplayChange circular;
    circular.kind = DisplayChangeKind::FORMAT;
    circular.format = VnaDisplay::DisplayFormat::SMITH;
    Require(Finish(fixture, fixture.service.ApplyDisplay(circular, fixture.service.Display().revision, OWNER)).ok,
        "切到 Smith");
    Require(!fixture.service.StimulusZoom().available && !Finish(fixture, Submit(fixture, Selection(fixture))).ok,
        "圆格式拒绝 Stimulus Zoom");
    circular.format = VnaDisplay::DisplayFormat::LOG_MAGNITUDE;
    Require(Finish(fixture, fixture.service.ApplyDisplay(circular, fixture.service.Display().revision, OWNER)).ok,
        "切到对数幅值格式");
    const auto logarithmic = fixture.service.Snapshot();
    auto positive = Selection(fixture);
    positive.min = 0.01;
    positive.max = 1.0;
    auto negative = positive;
    negative.min = 0.0;
    Require(!Finish(fixture, Submit(fixture, negative)).ok, "对数尺度拒绝非正选择");
    Unchanged(logarithmic, fixture.service.Snapshot());
    Require(Finish(fixture, Submit(fixture, positive)).ok && Finish(fixture, Out(fixture)).ok &&
                fixture.service.Display().traces.front().scale.min == logarithmic.display.traces.front().scale.min,
        "对数幅值沿既有正值尺度规则支持 Zoom 与撤回");
}

void HistoryPreservesLaterMarkerCatalogAndSelection()
{
    Fixture fixture;
    Prepare(fixture);
    const auto original = fixture.service.Snapshot();
    Require(Finish(fixture, Submit(fixture, Selection(fixture))).ok, "建立 Zoom 历史");
    DisplayChange marker;
    marker.kind = DisplayChangeKind::MARKER;
    marker.name = "Trc1";
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasFrequency = true;
    marker.marker.frequencyHz = (fixture.service.Settings().startHz + fixture.service.Settings().stopHz) / 2.0;
    Require(Finish(fixture, fixture.service.ApplyDisplay(marker, fixture.service.Display().revision, OWNER)).ok,
        "Zoom 后新增 Marker");
    Require(Finish(fixture, fixture.service.AddMeasurement(
                                fixture.service.Settings().revision, fixture.service.Display().revision, OWNER))
                .ok,
        "Zoom 后新增并选择其他迹线");
    DisplayChange scale;
    scale.kind = DisplayChangeKind::SCALE;
    scale.name = fixture.service.Display().selectedMeasurement;
    scale.scale.hasMin = true;
    scale.scale.hasMax = true;
    scale.scale.min = -70.0;
    scale.scale.max = 0.0;
    Require(Finish(fixture, fixture.service.ApplyDisplay(scale, fixture.service.Display().revision, OWNER)).ok,
        "修改其他迹线纵轴");
    const auto beforeOut = fixture.service.Snapshot();
    auto inactive = Selection(fixture);
    inactive.traceName = "Trc1";
    Require(!Finish(fixture, Submit(fixture, inactive)).ok, "框选必须针对活动迹线，不能借其他窗口的选择改旧迹线");
    Unchanged(beforeOut, fixture.service.Snapshot());
    Require(beforeOut.stimulusZoom.canZoomOut && Finish(fixture, Out(fixture)).ok, "另一迹线活动时撤回原 Zoom");
    const auto after = fixture.service.Snapshot();
    Require(after.display.selectedMeasurement == beforeOut.display.selectedMeasurement &&
                after.settings.measurements.size() == 2U && after.display.traces.back().scale.min == -70.0 &&
                after.display.traces.back().scale.max == 0.0 && after.display.traces.front().markers.Active().enabled &&
                after.display.traces.front().markers.Active().frequencyHz == marker.marker.frequencyHz &&
                after.display.traces.front().scale.min == original.display.traces.front().scale.min &&
                after.display.traces.front().scale.max == original.display.traces.front().scale.max,
        "撤回仅恢复原频率和目标 scale，不回滚新 Marker、目录、选择或其他迹线设置");
}

void ExternalChangesBlockHistoryAndCalibrationLocksZoom()
{
    for (unsigned field = 0U; field < 4U; ++field) {
        Fixture fixture;
        Prepare(fixture);
        Require(Finish(fixture, Submit(fixture, Selection(fixture))).ok, "建立待冲突历史");
        if (field == 0U) {
            VnaService::ConfigPatch frequency;
            frequency.hasStartHz = true;
            frequency.startHz = fixture.service.Settings().startHz + 10.0;
            Require(Finish(fixture,
                        fixture.service.ApplyConfiguration(frequency, fixture.service.Settings().revision, OWNER))
                        .ok,
                "外部修改频率");
        } else if (field < 3U) {
            DisplayChange display;
            display.kind = field == 1U ? DisplayChangeKind::SCALE : DisplayChangeKind::FORMAT;
            display.scale.hasMin = true;
            display.scale.min = fixture.service.Display().traces.front().scale.min + 1.0;
            display.format = VnaDisplay::DisplayFormat::PHASE;
            Require(
                Finish(fixture, fixture.service.ApplyDisplay(display, fixture.service.Display().revision, OWNER)).ok,
                "外部修改目标 scale 或格式");
        } else {
            Require(Finish(fixture, fixture.service.AddMeasurement(
                                        fixture.service.Settings().revision, fixture.service.Display().revision, OWNER))
                            .ok &&
                        Finish(fixture,
                            fixture.service.DeleteMeasurement("Trc1", fixture.service.Settings().revision, OWNER))
                            .ok,
                "外部删除原 Zoom 目标迹线");
        }
        const auto before = fixture.service.Snapshot();
        Require(!before.stimulusZoom.canZoomOut && !Finish(fixture, Out(fixture)).ok, "当前字段不匹配时禁止历史覆盖");
        Unchanged(before, fixture.service.Snapshot());
    }
    Fixture fixture;
    Prepare(fixture);
    VnaService::CalibrationChange calibration;
    calibration.kind = VnaService::CalibrationChangeKind::BEGIN;
    calibration.port1 = 1U;
    Require(
        Finish(fixture, fixture.service.ApplyCalibration(calibration, fixture.service.Calibration().revision, OWNER))
            .ok,
        "进入校准会话");
    const auto before = fixture.service.Snapshot();
    Require(!before.stimulusZoom.available && !Finish(fixture, Submit(fixture, Selection(fixture))).ok,
        "校准采集中锁定 Zoom");
    Unchanged(before, fixture.service.Snapshot());
}

void HistoryIsBoundedAndRestoresOneEntryAtATime()
{
    Fixture fixture;
    Prepare(fixture);
    for (unsigned index = 0U; index < 18U; ++index) {
        const auto &settings = fixture.service.Settings();
        StimulusZoomChange change;
        change.traceName = "Trc1";
        change.startHz = settings.startHz + 1.0;
        change.stopHz = settings.stopHz - 1.0;
        change.min = fixture.service.Display().traces.front().scale.min;
        change.max = fixture.service.Display().traces.front().scale.max;
        Require(Finish(fixture, Submit(fixture, change)).ok, "重复真实频率框选");
    }
    Require(fixture.service.StimulusZoom().depth == 16U, "Zoom 历史最多十六层");
    for (unsigned remaining = 16U; remaining > 0U; --remaining) {
        Require(Finish(fixture, Out(fixture)).ok && fixture.service.StimulusZoom().depth == remaining - 1U,
            "每次 Out 只撤回一层");
    }
    Require(fixture.service.Settings().startHz == 1000002.0 && !Finish(fixture, Out(fixture)).ok,
        "被有界历史淘汰的两层不可继续撤回");
}

void RecreatedTraceCannotReuseAnOldHistoryIdentity()
{
    Fixture fixture;
    Prepare(fixture);
    auto change = Selection(fixture);
    change.min = fixture.service.Display().traces.front().scale.min;
    change.max = fixture.service.Display().traces.front().scale.max;
    Require(
        Finish(fixture, Submit(fixture, change)).ok &&
            Finish(fixture, fixture.service.AddMeasurement(
                                fixture.service.Settings().revision, fixture.service.Display().revision, OWNER))
                .ok &&
            Finish(fixture, fixture.service.DeleteMeasurement("Trc1", fixture.service.Settings().revision, OWNER)).ok,
        "建立 X-only Zoom 后删除目标迹线");
    VnaService::Measurement replacement;
    replacement.name = "Trc1";
    Require(replacement.SetParameter("S21") && Finish(fixture, fixture.service.DefineMeasurement(replacement,
                                                                   fixture.service.Settings().revision, OWNER))
                                                   .ok,
        "使用相同名称、参数和默认 scale 建立新迹线");
    const auto before = fixture.service.Snapshot();
    Require(
        !before.stimulusZoom.canZoomOut && !Finish(fixture, Out(fixture)).ok, "同名重建不能复用旧迹线的 Zoom 历史身份");
    Unchanged(before, fixture.service.Snapshot());
}
}  // 匿名命名空间

int main()
{
    try {
        HoldZoomCommitsRealFrequenciesAndScaleTogether();
        ContinuousZoomWaitsForCompleteFrame();
        ManualZoomDoesNotAcquireAndFiniteZoomWaits();
        VerticalOnlyZoomKeepsManualCompletedFrameCurrent();
        DualCasAndDeviceFailurePreserveBothDomains();
        InvalidSelectionsAndNoopDoNotChangeState();
        HistoryPreservesLaterMarkerCatalogAndSelection();
        ExternalChangesBlockHistoryAndCalibrationLocksZoom();
        HistoryIsBoundedAndRestoresOneEntryAtATime();
        RecreatedTraceCannotReuseAnOldHistoryIdentity();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Stimulus Zoom 原子配置与安全历史测试通过\n";
    return 0;
}
