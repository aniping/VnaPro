/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"

namespace {
using VnaService::SweepMode;
using VnaService::TriggerSource;
using VnaServiceTest::Fixture;
const std::string OWNER = "WEB";

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
void Wait(Predicate predicate, const std::string &message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        Require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    Wait(
        [&fixture, &operation] {
            fixture.service.Poll();
            return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        },
        "等待扫描控制操作完成超时");
    return operation.get();
}

void RejectImmediately(const VnaService::Operation &operation, const std::string &message)
{
    Require(operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready && !operation.get().ok, message);
}

void Prepare(Fixture &fixture)
{
    Require(fixture.service.TryAcquireControl(OWNER), "取得 Web 控制权");
}

void SelectionDoesNotStartAndQueuedStartUsesLatestSelection()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER)).ok, "选择 Single");
    const auto selected = fixture.service.Snapshot();
    Require(selected.settings.selectedSweepMode == SweepMode::SINGLE &&
                selected.settings.sweepMode == SweepMode::HOLD && selected.core.state == VnaCore::CoreState::READY &&
                !selected.core.frame && selected.core.runId == 0U,
        "选择 Single 只能提交选项，不得隐式采集一帧");
    const auto source = fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER);
    const auto start = fixture.service.StartSweep(OWNER);
    Require(Finish(fixture, source).ok && Finish(fixture, start).ok, "按 FIFO 选择 Manual 后启动");
    const auto armed = fixture.service.Snapshot();
    Require(armed.settings.triggerSource == TriggerSource::MANUAL &&
                armed.settings.selectedSweepMode == SweepMode::SINGLE &&
                armed.settings.sweepMode == SweepMode::SINGLE && armed.core.state == VnaCore::CoreState::RUNNING &&
                !armed.core.frame,
        "Start 必须使用执行时的 Single/Manual 选项，只布防不采帧");
}

void SingleManualCompletesOneFrameAndRetainsSelection()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER)).ok &&
                Finish(fixture, fixture.service.StartSweep(OWNER)).ok,
        "布防 Single/Manual");
    fixture.device->Arm();
    const auto trigger = fixture.service.Trigger(OWNER);
    Wait(
        [&fixture] {
            fixture.service.Poll();
            return fixture.device->HasEntered();
        },
        "手动触发进入真实采集关卡");
    Require(trigger.wait_for(std::chrono::seconds(0)) != std::future_status::ready,
        "Manual Trigger 必须等待完整帧及停止屏障");
    Require(!fixture.service.Trigger(OWNER).get().ok, "拒绝与有限帧重叠的触发");
    fixture.device->Release();
    Require(Finish(fixture, trigger).ok, "手动采集完成");
    const auto done = fixture.service.Snapshot();
    Require(done.core.state == VnaCore::CoreState::READY && done.core.frame && done.core.frame->frameSeq == 1U &&
                done.settings.sweepMode == SweepMode::HOLD && done.settings.selectedSweepMode == SweepMode::SINGLE &&
                done.settings.triggerSource == TriggerSource::MANUAL,
        "Single 只产生一帧且完成后保留 Single/Manual");
    Require(!Finish(fixture, fixture.service.Trigger(OWNER)).ok, "Single 完成后必须重新 Start 才能触发");
    Require(Finish(fixture, fixture.service.StartSweep(OWNER)).ok && Finish(fixture, fixture.service.Trigger(OWNER)).ok,
        "保留 Single/Manual 后可再次 Start 与触发");
}

void ContinuousManualStaysArmedAndStopRetainsChoices()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.StartSweep(OWNER)).ok,
        "布防 Continuous/Manual");
    Require(!fixture.core.ReadView().frame, "Manual 布防不得自动采集");
    Require(Finish(fixture, fixture.service.Trigger(OWNER)).ok, "第一次 Manual Trigger");
    const auto first = fixture.service.Snapshot();
    Require(first.core.state == VnaCore::CoreState::RUNNING && first.core.frame->frameSeq == 1U,
        "Continuous/Manual 完成一帧后保持布防");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Require(fixture.core.ReadView().frame->frameSeq == 1U, "Manual 不得自行重复采帧");
    Require(Finish(fixture, fixture.service.Trigger(OWNER)).ok &&
                fixture.core.ReadView().frame->frameSeq == first.core.frame->frameSeq + 1U,
        "每次 Manual Trigger 恰好产生一帧");
    Require(Finish(fixture, fixture.service.StopSweep(OWNER)).ok, "停止手动扫描");
    const auto stopped = fixture.service.Snapshot();
    Require(stopped.core.state == VnaCore::CoreState::READY && stopped.settings.sweepMode == SweepMode::HOLD &&
                stopped.settings.selectedSweepMode == SweepMode::CONTINUOUS &&
                stopped.settings.triggerSource == TriggerSource::MANUAL,
        "Stop 保留 Continuous/Manual 选项");
}

void ContinuousSelectionStartsFreeRunAfterSingle()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER)).ok &&
                Finish(fixture, fixture.service.StartSweep(OWNER)).ok,
        "显式启动 Free Run/Single");
    const auto single = fixture.service.Snapshot();
    Require(single.core.frame && single.core.frame->frameSeq == 1U && single.core.state == VnaCore::CoreState::READY &&
                single.settings.selectedSweepMode == SweepMode::SINGLE && single.settings.sweepMode == SweepMode::HOLD,
        "Free Run/Single 完成一次并保留选择");
    Require(Finish(fixture, fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER)).ok &&
                fixture.core.ReadView().runId > single.core.runId &&
                fixture.core.ReadView().state == VnaCore::CoreState::RUNNING &&
                fixture.service.Settings().selectedSweepMode == SweepMode::CONTINUOUS &&
                fixture.service.Settings().sweepMode == SweepMode::CONTINUOUS,
        "Single 停止后改变为 Continuous，应直接启动新的 Free Run 运行");
    Wait(
        [&fixture] {
            const auto frame = fixture.core.ReadView().frame;
            return frame && frame->frameSeq >= 2U;
        },
        "Free Run 持续产生完整帧");
    const auto run = fixture.core.ReadView().runId;
    Require(
        Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok, "运行中切换到 Manual");
    const auto changed = fixture.service.Snapshot();
    Require(changed.core.state == VnaCore::CoreState::RUNNING && changed.core.runId > run &&
                changed.settings.sweepMode == SweepMode::CONTINUOUS &&
                changed.settings.triggerSource == TriggerSource::MANUAL,
        "运行中切换触发源经 Stop 屏障后恢复原扫描方式，Manual 保持布防");
    Require(!changed.core.frame || changed.core.frame->runId != changed.core.runId,
        "新 Manual 运行在触发前不采集，允许保留上一运行的完整帧");
    Require(Finish(fixture, fixture.service.Trigger(OWNER)).ok && Finish(fixture, fixture.service.Trigger(OWNER)).ok,
        "切到 Manual 后无需再次 Start 即可连续手动触发");
    Require(
        fixture.core.ReadView().frame->frameSeq == 2U && fixture.core.ReadView().state == VnaCore::CoreState::RUNNING,
        "每次触发生成一帧且保持 Continuous 布防");
}

void ContinuousSelectionArmsManualAfterStoppedSingle()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER)).ok,
        "准备停止的 Single/Manual");
    Require(
        Finish(fixture, fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER)).ok, "改变为 Continuous/Manual");
    const auto armed = fixture.service.Snapshot();
    Require(armed.core.state == VnaCore::CoreState::RUNNING && armed.core.runId != 0U && !armed.core.frame &&
                armed.settings.triggerSource == TriggerSource::MANUAL &&
                armed.settings.sweepMode == SweepMode::CONTINUOUS &&
                armed.settings.selectedSweepMode == SweepMode::CONTINUOUS && !armed.finitePending,
        "改变为 Continuous 保留 Manual，直接布防但不自行采帧");
    Require(Finish(fixture, fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER)).ok &&
                fixture.core.ReadView().runId == armed.core.runId && !fixture.core.ReadView().frame,
        "重复选择已选 Continuous 不重启布防或触发采集");
    Require(Finish(fixture, fixture.service.Trigger(OWNER)).ok && Finish(fixture, fixture.service.Trigger(OWNER)).ok &&
                fixture.core.ReadView().frame->frameSeq == 2U,
        "选择 Continuous 后无需 Start 即可按 Manual 逐帧触发");
    Require(Finish(fixture, fixture.service.StopSweep(OWNER)).ok, "停止 Continuous/Manual");
    const auto stopped = fixture.service.Snapshot();
    Require(Finish(fixture, fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER)).ok &&
                fixture.core.ReadView().state == VnaCore::CoreState::READY &&
                fixture.core.ReadView().runId == stopped.core.runId &&
                fixture.core.ReadView().frame == stopped.core.frame &&
                fixture.service.Settings().sweepMode == SweepMode::HOLD,
        "Stop 后重复选择已选 Continuous 延续幂等合同，不推定为 Restart");
}

void ContinuousSelectionWaitsForRunningSingleFrame()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.RunSelectedSingle(OWNER)).ok,
        "准备 Single/Manual 布防");
    const auto singleRun = fixture.core.ReadView().runId;
    fixture.device->Arm();
    const auto trigger = fixture.service.Trigger(OWNER);
    const auto continuous = fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER);
    Wait(
        [&fixture] {
            fixture.service.Poll();
            return fixture.device->HasEntered();
        },
        "等待正在执行的 Single 帧进入关卡");
    Require(continuous.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Settings().selectedSweepMode == SweepMode::SINGLE &&
                fixture.core.ReadView().runId == singleRun,
        "Single 帧完成前不能提前提交 Continuous 或重建运行身份");
    fixture.device->Release();
    Require(Finish(fixture, trigger).ok && Finish(fixture, continuous).ok, "Single 完整帧与停止屏障后切换 Continuous");
    const auto armed = fixture.service.Snapshot();
    Require(armed.core.state == VnaCore::CoreState::RUNNING && armed.core.runId > singleRun && armed.core.frame &&
                armed.core.frame->runId == singleRun && armed.core.frame->frameSeq == 1U &&
                armed.settings.selectedSweepMode == SweepMode::CONTINUOUS &&
                armed.settings.sweepMode == SweepMode::CONTINUOUS &&
                armed.settings.triggerSource == TriggerSource::MANUAL && !armed.finitePending,
        "保留完整 Single 帧，Continuous/Manual 创建新布防运行，不额外采帧或遗留有限锁");
    Require(Finish(fixture, fixture.service.Trigger(OWNER)).ok &&
                fixture.core.ReadView().frame->runId == armed.core.runId &&
                fixture.core.ReadView().frame->frameSeq == 1U,
        "显式 Manual Trigger 才采集新 Continuous 运行的第一帧");
}

void SourceChangeWaitsForCompleteFrameAndUsesQueuedRunIntent()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.StartSweep(OWNER)).ok, "启动连续 Free Run");
    fixture.device->Arm();
    Wait(
        [&fixture] {
            return fixture.device->HasEntered();
        },
        "连续帧进入阻塞关卡");
    const auto before = fixture.service.Snapshot();
    const auto manual = fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER);
    fixture.service.Poll();
    Require(manual.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Settings().triggerSource == TriggerSource::IMMEDIATE &&
                fixture.core.ReadView().runId == before.core.runId,
        "旧完整帧结束前不提交新触发源或新运行身份");
    fixture.device->Release();
    Require(Finish(fixture, manual).ok && fixture.core.ReadView().state == VnaCore::CoreState::RUNNING &&
                fixture.core.ReadView().runId > before.core.runId &&
                fixture.core.ReadView().frame->runId == before.core.runId,
        "完整帧停止屏障后创建无采帧的 Manual 布防运行");
    const auto armedRun = fixture.core.ReadView().runId;
    const auto retainedFrame = fixture.core.ReadView().frame;
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok &&
                fixture.core.ReadView().runId == armedRun && fixture.core.ReadView().frame == retainedFrame,
        "重复选择 Manual 幂等，不重启或采帧");
    const auto stop = fixture.service.StopSweep(OWNER);
    const auto freeRun = fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER);
    Require(Finish(fixture, stop).ok && Finish(fixture, freeRun).ok, "FIFO 先停止再切 Free Run");
    const auto stopped = fixture.service.Snapshot();
    Require(stopped.core.state == VnaCore::CoreState::READY && stopped.core.runId == armedRun &&
                stopped.settings.sweepMode == SweepMode::HOLD &&
                stopped.settings.triggerSource == TriggerSource::IMMEDIATE && stopped.core.frame == retainedFrame,
        "停止后切源保持停止，不能使用受理时的旧布防意图重启");
    const auto start = fixture.service.StartSweep(OWNER);
    const auto queuedManual = fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER);
    Require(Finish(fixture, start).ok && Finish(fixture, queuedManual).ok &&
                fixture.core.ReadView().state == VnaCore::CoreState::RUNNING &&
                fixture.service.Settings().triggerSource == TriggerSource::MANUAL,
        "FIFO 先启动再切源保留执行时的运行意图，不能使用受理时的旧停止状态");
    const auto continuousFreeRun = fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER);
    Require(fixture.service.Snapshot().finitePending, "切 Free Run 在执行前保守预留有限采集准入");
    fixture.service.Poll();
    Require(!fixture.service.Snapshot().finitePending, "执行时确认 Continuous 后应立即清除有限采集预留");
    Require(Finish(fixture, continuousFreeRun).ok && !fixture.service.Snapshot().finitePending,
        "Continuous/Free Run 切源完成后不残留有限采集锁");
    const auto duplicateFreeRun = fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER);
    const auto continuousRun = fixture.core.ReadView().runId;
    Require(Finish(fixture, duplicateFreeRun).ok && !fixture.service.Snapshot().finitePending &&
                fixture.core.ReadView().runId == continuousRun,
        "重复源选择清除临时预留，不重启或残留有限采集锁");
}

void ArmedSingleSwitchToFreeRunCompletesOneProtectedFrame()
{
    Fixture fixture;
    Prepare(fixture);
    Require(
        Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok, "准备 Manual 停止状态");
    const auto single = fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER);
    const auto start = fixture.service.StartSweep(OWNER);
    const auto freeRun = fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER);
    Require(Finish(fixture, single).ok && Finish(fixture, start).ok, "FIFO 前序选择 Single 并按 Manual 布防");
    const auto run = fixture.core.ReadView().runId;
    fixture.device->Arm();
    RejectImmediately(fixture.service.Trigger(OWNER), "切到 Free Run 的有限采集在首次 Poll 前即阻止重叠触发");
    RejectImmediately(fixture.service.StartSweep(OWNER), "切到 Free Run 的有限采集在首次 Poll 前即阻止重叠启动");
    RejectImmediately(fixture.service.RunSelectedSingle(OWNER), "切到 Free Run 的有限采集在首次 Poll 前即阻止快捷启动");
    Wait(
        [&fixture] {
            fixture.service.Poll();
            return fixture.device->HasEntered();
        },
        "切源后自动进入 Single/Free Run 新帧采集");
    Require(freeRun.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Snapshot().finitePending && !fixture.service.Trigger(OWNER).get().ok,
        "切源操作必须等完整帧与最终 Stop 屏障，过程中保持有限采集准入保护");
    fixture.device->Release();
    Require(Finish(fixture, freeRun).ok, "Single/Free Run 完整帧与停止屏障完成");
    const auto done = fixture.service.Snapshot();
    Require(done.core.state == VnaCore::CoreState::READY && done.core.runId > run && done.core.frame &&
                done.core.frame->frameSeq == 1U && done.settings.sweepMode == SweepMode::HOLD &&
                done.settings.selectedSweepMode == SweepMode::SINGLE &&
                done.settings.triggerSource == TriggerSource::IMMEDIATE && !done.finitePending,
        "切源只完成一帧，保留 Single/Free Run 并释放有限采集保护");
    const auto completedRun = done.core.runId;
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER)).ok &&
                fixture.core.ReadView().runId == completedRun && fixture.core.ReadView().frame == done.core.frame,
        "停止后重复选择 Free Run 不创建运行或额外帧");
}

void SourceRestartRejectsFaultBeforeStartTicketConsumption()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.StartSweep(OWNER)).ok,
        "准备 Continuous/Manual 布防");
    const auto before = fixture.service.Snapshot();
    fixture.device->Arm(true);
    const auto freeRun = fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER);
    fixture.service.Poll();
    // 同运行的幂等 Stop 票据是核心 FIFO 屏障，确保服务首次 Stop 的票据已经就绪。
    Require(fixture.core.Stop(before.core.runId).get().ok, "等待切源 Stop 屏障完成");
    fixture.service.Poll();
    Wait(
        [&fixture] {
            return fixture.device->HasEntered();
        },
        "不推进服务，等待新 Continuous 首帧进入关卡");
    Require(freeRun.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Settings().triggerSource == TriggerSource::MANUAL,
        "Start 成功票据未消费前，不应报告切源成功或提交候选源");
    fixture.device->Release();
    Wait(
        [&fixture] {
            return fixture.core.ReadView().state == VnaCore::CoreState::FAULTED;
        },
        "不推进服务，等待首帧故障先发布");
    const auto result = Finish(fixture, freeRun);
    Require(
        !result.ok && result.message == "controlled acquisition failure", "首帧已故障时切源必须失败并返回原始设备错误");
    const auto failed = fixture.service.Snapshot();
    Require(failed.settings.triggerSource == TriggerSource::MANUAL &&
                failed.settings.selectedSweepMode == SweepMode::CONTINUOUS &&
                failed.settings.sweepMode == SweepMode::HOLD && failed.core.state == VnaCore::CoreState::FAULTED &&
                failed.core.runId > before.core.runId && !failed.pending && !failed.finitePending,
        "未提交候选触发源，结束运行并释放失败切源的队列及有限采集保护");
}

void PermissionAndInvalidChoiceDoNotMutateSettings()
{
    Fixture fixture;
    Require(!fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER).get().ok &&
                !fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER).get().ok &&
                !fixture.service.StartSweep(OWNER).get().ok && !fixture.service.StopSweep(OWNER).get().ok,
        "所有新入口都要求有效控制权");
    Prepare(fixture);
    Require(!fixture.service.SelectSweepMode(SweepMode::HOLD, OWNER).get().ok &&
                !fixture.service.SelectTriggerSource(static_cast<TriggerSource>(99), OWNER).get().ok,
        "HOLD 不是可选扫描模式，不支持的触发源必须拒绝");
    Require(fixture.service.Settings().selectedSweepMode == SweepMode::CONTINUOUS &&
                fixture.service.Settings().triggerSource == TriggerSource::IMMEDIATE,
        "拒绝请求不能修改所选模式和触发源");
}

void RepeatedSelectionsAreIdempotentAndChangesWaitForStop()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.StartSweep(OWNER)).ok, "启动缺省 Free Run/Continuous");
    const auto run = fixture.core.ReadView().runId;
    Require(Finish(fixture, fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER)).ok &&
                Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER)).ok &&
                fixture.core.ReadView().state == VnaCore::CoreState::RUNNING && fixture.core.ReadView().runId == run,
        "重复选择当前值不停止采集或改变运行身份");
    fixture.device->Arm();
    Wait(
        [&fixture] {
            return fixture.device->HasEntered();
        },
        "等待当前连续帧进入关卡");
    const auto select = fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER);
    fixture.service.Poll();
    Require(select.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Settings().selectedSweepMode == SweepMode::CONTINUOUS,
        "选择变化必须等待完整帧停止屏障，不能提前发布新选择");
    fixture.device->Release();
    Require(Finish(fixture, select).ok && fixture.core.ReadView().state == VnaCore::CoreState::READY &&
                fixture.core.ReadView().runId == run &&
                fixture.service.Settings().selectedSweepMode == SweepMode::SINGLE,
        "屏障后提交 Single 选项，保留已结束运行身份");
    Require(Finish(fixture, fixture.service.StartSweep(OWNER)).ok && fixture.core.ReadView().runId > run,
        "再次 Start 才创建新运行身份");
}

void ShortcutPreservesSourceAndFaultPreservesChoices()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::MANUAL, OWNER)).ok &&
                Finish(fixture, fixture.service.RunSelectedSingle(OWNER)).ok,
        "快捷单次原子选择 Single 并按 Manual 布防");
    Require(fixture.service.Settings().selectedSweepMode == SweepMode::SINGLE &&
                fixture.service.Settings().triggerSource == TriggerSource::MANUAL && !fixture.core.ReadView().frame,
        "快捷单次不得把 Manual 改回 Free Run 或提前采帧");
    fixture.device->Arm(true);
    const auto trigger = fixture.service.Trigger(OWNER);
    Wait(
        [&fixture] {
            fixture.service.Poll();
            return fixture.device->HasEntered();
        },
        "进入故障注入采集关卡");
    fixture.device->Release();
    Require(!Finish(fixture, trigger).ok && fixture.core.ReadView().state == VnaCore::CoreState::FAULTED &&
                fixture.service.Settings().sweepMode == SweepMode::HOLD &&
                fixture.service.Settings().selectedSweepMode == SweepMode::SINGLE &&
                fixture.service.Settings().triggerSource == TriggerSource::MANUAL,
        "采集故障结束运行但不丢失用户选择");
    Require(!Finish(fixture, fixture.service.StartSweep(OWNER)).ok &&
                !Finish(fixture, fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER)).ok &&
                !Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER)).ok,
        "故障后不得启动或偷偷提交其他模式与触发源");
}

void CalibrationLocksControlAndRetainsStoppedSelection()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SelectSweepMode(SweepMode::SINGLE, OWNER)).ok, "校准前选择 Single");
    VnaService::CalibrationChange begin;
    begin.kind = VnaService::CalibrationChangeKind::BEGIN;
    begin.port1 = 1U;
    Require(Finish(fixture, fixture.service.ApplyCalibration(begin, fixture.service.Calibration().revision, OWNER)).ok,
        "进入校准采集会话");
    Require(!Finish(fixture, fixture.service.SelectSweepMode(SweepMode::CONTINUOUS, OWNER)).ok &&
                !Finish(fixture, fixture.service.SelectTriggerSource(TriggerSource::IMMEDIATE, OWNER)).ok &&
                !Finish(fixture, fixture.service.StartSweep(OWNER)).ok &&
                !Finish(fixture, fixture.service.RunSelectedSingle(OWNER)).ok,
        "校准会话锁定模式、触发源和启动动作");
    Require(Finish(fixture, fixture.service.StopSweep(OWNER)).ok, "校准会话仍允许 Stop");
    VnaService::CalibrationChange cancel;
    cancel.kind = VnaService::CalibrationChangeKind::CANCEL;
    cancel.sessionId = fixture.service.Calibration().sessionId;
    Require(Finish(fixture, fixture.service.ApplyCalibration(cancel, fixture.service.Calibration().revision, OWNER)).ok,
        "取消校准恢复进入前停止状态");
    Require(fixture.service.Settings().selectedSweepMode == SweepMode::SINGLE &&
                fixture.service.Settings().triggerSource == TriggerSource::IMMEDIATE &&
                fixture.service.Settings().sweepMode == SweepMode::HOLD &&
                fixture.core.ReadView().state == VnaCore::CoreState::READY && !fixture.core.ReadView().frame,
        "校准临时模式与触发源不得覆盖所选 Single/Free Run 或触发采集");
}
}  // 匿名命名空间

int main()
{
    try {
        SelectionDoesNotStartAndQueuedStartUsesLatestSelection();
        SingleManualCompletesOneFrameAndRetainsSelection();
        ContinuousManualStaysArmedAndStopRetainsChoices();
        ContinuousSelectionStartsFreeRunAfterSingle();
        ContinuousSelectionArmsManualAfterStoppedSingle();
        ContinuousSelectionWaitsForRunningSingleFrame();
        SourceChangeWaitsForCompleteFrameAndUsesQueuedRunIntent();
        ArmedSingleSwitchToFreeRunCompletesOneProtectedFrame();
        SourceRestartRejectsFaultBeforeStartTicketConsumption();
        PermissionAndInvalidChoiceDoNotMutateSettings();
        RepeatedSelectionsAreIdempotentAndChangesWaitForStop();
        ShortcutPreservesSourceAndFaultPreservesChoices();
        CalibrationLocksControlAndRetainsStoppedSelection();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "扫描模式、启动动作与触发源边界测试通过\n";
    return 0;
}
