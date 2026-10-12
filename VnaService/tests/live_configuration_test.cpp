/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fixture.h"
#include "vna_core/frame_layout.h"

namespace {
using VnaServiceTest::Fixture;

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
        "等待实时扫频配置事务超时");
    return operation.get();
}

void Prepare(Fixture &fixture, const std::string &owner = "SCPI")
{
    const auto initial = fixture.service.Snapshot();
    Require(initial.settings.sweepMode == VnaService::SweepMode::HOLD && !initial.core.frame,
        "服务夹具初始化仍保持 HOLD，启动扫描只由服务器缺省配置决定");
    Require(fixture.service.TryAcquireControl(owner), "取得实时配置控制权");
    VnaService::ConfigPatch patch;
    patch.hasPoints = true;
    patch.points = 3U;
    Require(Finish(fixture, fixture.service.ApplyConfiguration(patch, initial.settings.revision, owner)).ok,
        "准备独立三点扫描配置");
}

void Continuous(Fixture &fixture, const std::string &owner = "SCPI")
{
    Require(Finish(fixture, fixture.service.RunContinuous(owner)).ok, "启动连续扫描");
    Wait(
        [&fixture] {
            return fixture.core.ReadView().frame != nullptr;
        },
        "等待连续扫描首帧超时");
}

void BlockFrame(Fixture &fixture)
{
    fixture.device->Arm();
    Wait(
        [&fixture] {
            return fixture.device->HasEntered();
        },
        "等待完整帧采集进入阻塞关卡");
}

void CurrentFrame(Fixture &fixture)
{
    Wait(
        [&fixture] {
            const auto snapshot = fixture.service.Snapshot();
            return snapshot.core.frame && snapshot.core.frame->configRevision == snapshot.settings.acquisitionRevision;
        },
        "等待新物理配置的完整帧");
}

void SameRun(const VnaService::InstrumentSnapshot &before, const VnaService::InstrumentSnapshot &after)
{
    Require(after.core.state == VnaCore::CoreState::RUNNING && after.core.runId == before.core.runId &&
                after.settings.sweepMode == before.settings.sweepMode &&
                after.settings.triggerSource == before.settings.triggerSource &&
                after.controlOwner == before.controlOwner,
        "实时物理配置不能停止扫描、重建运行身份、改变触发源或转移控制权");
}

void ContinuousPointsWaitForCompleteFrame()
{
    Fixture fixture;
    Prepare(fixture);
    Continuous(fixture);
    BlockFrame(fixture);
    const auto before = fixture.service.Snapshot();
    const auto retained = before.core.frame;
    const auto operation = fixture.service.SetPoints(5U, before.settings.revision);
    fixture.service.Poll();
    Require(operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready,
        "CONT 期间点数修改必须等待完整帧边界，不能被 HOLD 准入守卫提前拒绝");
    Require(fixture.service.Settings().revision == before.settings.revision &&
                fixture.service.Settings().points == 3U && fixture.core.ReadView().frame == retained,
        "旧完整帧结束前不能发布新点数或逻辑版本");
    fixture.device->Release();
    Require(Finish(fixture, operation).ok, "完整帧边界提交新扫描点数");
    CurrentFrame(fixture);
    const auto after = fixture.service.Snapshot();
    SameRun(before, after);
    Require(after.settings.points == 5U && after.settings.revision == before.settings.revision + 1U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision + 1U &&
                after.core.frame->plan.frequencyHz.size() == 5U &&
                after.core.frame->ports.front().iq.size() == 5U * VnaCore::IQ_VALUES_PER_POINT &&
                after.core.frame->frameSeq > retained->frameSeq && retained->plan.frequencyHz.size() == 3U &&
                retained->configRevision == before.settings.acquisitionRevision,
        "新完整帧使用全部新点数，旧快照保留原三点配置与身份");
}

void ContinuousPatchCommitsAllPhysicalFields()
{
    Fixture fixture;
    Prepare(fixture, "WEB");
    Continuous(fixture, "WEB");
    const auto before = fixture.service.Snapshot();
    VnaService::ConfigPatch patch;
    patch.hasStartHz = true;
    patch.startHz = 20e6;
    patch.hasStopHz = true;
    patch.stopHz = 80e6;
    patch.hasPoints = true;
    patch.points = 7U;
    patch.hasIfBandwidthHz = true;
    patch.ifBandwidthHz = 10000.0;
    Require(Finish(fixture, fixture.service.ApplyConfiguration(patch, before.settings.revision, "WEB")).ok,
        "CONT 期间原子修改起止频率、点数与 IF 带宽");
    CurrentFrame(fixture);
    const auto after = fixture.service.Snapshot();
    SameRun(before, after);
    Require(after.settings.startHz == 20e6 && after.settings.stopHz == 80e6 && after.settings.points == 7U &&
                after.settings.ifBandwidthHz == 10000.0 && after.core.frame->plan.frequencyHz.front() == 20e6 &&
                after.core.frame->plan.frequencyHz.back() == 80e6 && after.core.frame->plan.frequencyHz.size() == 7U &&
                after.core.frame->plan.ifBandwidthRaw == 10000U &&
                after.settings.revision == before.settings.revision + 1U,
        "一个完整帧不得混合补丁前后的物理字段");
}

void ConfigurationFailurePreservesContinuousRun()
{
    Fixture fixture;
    Prepare(fixture);
    Continuous(fixture);
    const auto before = fixture.service.Snapshot();
    fixture.device->FailNextPlanPreparation();
    Require(
        !Finish(fixture, fixture.service.SetPoints(5U, before.settings.revision)).ok, "设备准备失败拒绝实时物理配置");
    const auto after = fixture.service.Snapshot();
    SameRun(before, after);
    Require(after.settings.points == before.settings.points && after.settings.revision == before.settings.revision &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                after.core.configRevision == before.core.configRevision,
        "准备失败不发布任何物理字段或版本");
    Wait(
        [&fixture, &after] {
            return fixture.core.ReadView().frame->frameSeq > after.core.frame->frameSeq;
        },
        "准备失败后仍按旧配置持续发布完整帧");
}

void ManualArmedConfigurationDoesNotTrigger()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.SetTriggerSource(VnaService::TriggerSource::MANUAL)).ok &&
                Finish(fixture, fixture.service.SetSweepMode(VnaService::SweepMode::CONTINUOUS)).ok,
        "布防手动连续模式");
    const auto before = fixture.service.Snapshot();
    Require(!before.core.frame, "手动布防不自动采集");
    Require(Finish(fixture, fixture.service.SetPoints(5U, before.settings.revision)).ok, "手动布防期间修改扫描点数");
    const auto after = fixture.service.Snapshot();
    SameRun(before, after);
    Require(!after.core.frame && after.settings.points == 5U, "计划更新不能暗改 IMM 或隐式触发");
    Require(Finish(fixture, fixture.service.Trigger()).ok &&
                fixture.core.ReadView().frame->plan.frequencyHz.size() == 5U &&
                fixture.core.ReadView().frame->frameSeq == 1U,
        "显式手动触发后才采集新计划第一帧");
}

void FiniteFrameFinishesBeforeQueuedConfiguration()
{
    Fixture fixture;
    Prepare(fixture);
    fixture.device->Arm();
    const auto single = fixture.service.RunSingle("SCPI");
    Wait(
        [&fixture] {
            fixture.service.Poll();
            return fixture.device->HasEntered();
        },
        "等待单次有限帧进入阻塞关卡");
    const auto before = fixture.service.Snapshot();
    const auto configuration = fixture.service.SetPoints(5U, before.settings.revision);
    fixture.service.Poll();
    Require(configuration.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Settings().points == 3U,
        "物理配置不能越过已接受的有限测量屏障");
    fixture.device->Release();
    Require(Finish(fixture, single).ok, "先完成旧三点单次帧并返回 HOLD");
    const auto frame = fixture.core.ReadView().frame;
    Require(frame && frame->plan.frequencyHz.size() == 3U && fixture.service.Settings().points == 3U,
        "旧有限测量使用其原始配置完整结束");
    Require(Finish(fixture, configuration).ok && fixture.service.Settings().points == 5U &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                fixture.core.ReadView().state == VnaCore::CoreState::READY && fixture.core.ReadView().frame == frame &&
                !fixture.service.MeasurementData().ok,
        "排队物理配置保持 HOLD，并拒绝把旧有限帧标为新配置当前数据");
}

void LiveConfigurationRechecksCasAndOwner()
{
    Fixture fixture;
    Prepare(fixture);
    Continuous(fixture);
    BlockFrame(fixture);
    const auto before = fixture.service.Snapshot();
    const auto first = fixture.service.SetPoints(5U, before.settings.revision);
    const auto stale = fixture.service.SetPoints(7U, before.settings.revision);
    fixture.device->Release();
    Require(Finish(fixture, first).ok && !Finish(fixture, stale).ok, "执行时拒绝被前一个实时配置覆盖的逻辑版本");
    VnaService::ConfigPatch patch;
    patch.hasPoints = true;
    patch.points = 9U;
    Require(
        !Finish(fixture, fixture.service.ApplyConfiguration(patch, fixture.service.Settings().revision, "OTHER")).ok,
        "实时配置保留控制权保护");
    const auto after = fixture.service.Snapshot();
    SameRun(before, after);
    Require(after.settings.points == 5U && after.settings.revision == before.settings.revision + 1U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision + 1U,
        "竞争或越权失败不留下额外点数与版本提交");
}
}  // 匿名命名空间

int main()
{
    try {
        ContinuousPointsWaitForCompleteFrame();
        ContinuousPatchCommitsAllPhysicalFields();
        ConfigurationFailurePreservesContinuousRun();
        ManualArmedConfigurationDoesNotTrigger();
        FiniteFrameFinishesBeforeQueuedConfiguration();
        LiveConfigurationRechecksCasAndOwner();
        std::cout << "PASS 实时扫频配置、完整帧边界、触发状态、控制权与版本保护\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
