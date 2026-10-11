/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fixture.h"
#include "vna_core/frame_layout.h"

namespace {
using VnaServiceTest::Fixture;
constexpr std::chrono::seconds TEST_TIMEOUT(3);

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
void WaitFor(Predicate predicate, const std::string &message)
{
    const auto deadline = std::chrono::steady_clock::now() + TEST_TIMEOUT;
    while (!predicate()) {
        Require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    WaitFor(
        [&fixture, &operation] {
            fixture.service.Poll();
            return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        },
        "等待服务操作超时");
    return operation.get();
}

VnaService::Measurement SParameter(unsigned receiver, unsigned source = 1U)
{
    VnaService::Measurement measurement;
    measurement.name = "Trc1";
    measurement.kind = VnaService::MeasurementKind::S_PARAMETER;
    measurement.receiverKind = 'b';
    measurement.receiverPort = receiver;
    measurement.sourcePort = source;
    return measurement;
}

void ClaimAndPrepare(Fixture &fixture)
{
    Require(fixture.service.TryAcquireControl(), "取得 SCPI 控制权");
    Require(Finish(fixture, fixture.service.SetPoints(3U)).ok, "准备独立三点扫描配置");
    std::vector<double> samples(VnaCore::IQ_VALUES_PER_POINT, 7.0);
    // a1=2+4i，b1=2.5i，a2=4+8i，b2=2.5；独立期望 S11=0.5+0.25i、S22=0.125-0.25i。
    samples[0U] = 2.0;
    samples[1U] = 4.0;
    samples[2U] = 0.0;
    samples[3U] = 2.5;
    samples[4U] = 4.0;
    samples[5U] = 8.0;
    samples[6U] = 2.5;
    samples[7U] = 0.0;
    fixture.device->SetSamples(std::move(samples));
}

void StartContinuous(Fixture &fixture)
{
    Require(Finish(fixture, fixture.service.RunContinuous("SCPI")).ok, "启动连续采集");
    WaitFor(
        [&fixture] {
            return fixture.core.ReadView().frame != nullptr;
        },
        "等待连续采集首帧超时");
}

void BlockNextFrame(Fixture &fixture)
{
    fixture.device->Arm();
    WaitFor(
        [&fixture] {
            return fixture.device->HasEntered();
        },
        "等待采集进入阻塞关卡超时");
}

void RequireContinuousRun(const VnaService::InstrumentSnapshot &before, const VnaService::InstrumentSnapshot &after)
{
    Require(after.settings.sweepMode == VnaService::SweepMode::CONTINUOUS &&
                after.core.state == VnaCore::CoreState::RUNNING && after.core.runId == before.core.runId,
        "测量切换不能停止或重启连续运行");
}

void RequireRatio(Fixture &fixture, double real, double imaginary)
{
    const auto data = fixture.service.MeasurementData();
    Require(data.ok && data.values.size() == 6U, "从完整三点原始帧计算当前测量");
    for (std::size_t index = 0U; index < data.values.size(); index += 2U) {
        Require(std::fabs(data.values[index] - real) < 1e-12 && std::fabs(data.values[index + 1U] - imaginary) < 1e-12,
            "测量数据必须符合独立给定的复比值");
    }
}

void ContinuousCoveredMeasurementUsesSameFrame()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    StartContinuous(fixture);
    BlockNextFrame(fixture);
    fixture.device->ArmPlan();
    const auto before = fixture.service.Snapshot();
    const auto operation = fixture.service.UpdateMeasurement(SParameter(1U), before.settings.revision);
    const auto result = Finish(fixture, operation);
    Require(result.ok, "CONT 期间 S21 切换 S11 被拒绝：" + result.message);
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.revision == before.settings.revision + 1U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                after.core.configRevision == before.core.configRevision && after.core.frame == before.core.frame,
        "已有需求只提交逻辑配置版本，并复用同一完整原始帧");
    RequireRatio(fixture, 0.5, 0.25);
    Require(!fixture.device->HasPlanEntered(), "覆盖已有需求的切换不重新下发设备计划");
}

void RequireUnchangedFrame(const VnaCore::MeasurementFrame &frame, const VnaCore::MeasurementFrame &copy)
{
    Require(frame.runId == copy.runId && frame.configRevision == copy.configRevision &&
                frame.frameSeq == copy.frameSeq && frame.channelId == copy.channelId &&
                frame.plan.frequencyHz == copy.plan.frequencyHz && frame.plan.frequencyRaw == copy.plan.frequencyRaw &&
                frame.plan.receiverMask == copy.plan.receiverMask && frame.plan.sourcePorts == copy.plan.sourcePorts &&
                frame.plan.tableData == copy.plan.tableData && frame.plan.ifBandwidthRaw == copy.plan.ifBandwidthRaw &&
                frame.ports.size() == copy.ports.size(),
        "保留的旧完整帧身份、频轴和物理计划必须深度不可变");
    for (std::size_t index = 0U; index < frame.ports.size(); ++index) {
        Require(frame.ports[index].sourcePort == copy.ports[index].sourcePort &&
                    frame.ports[index].iq == copy.ports[index].iq,
            "保留的旧帧源端口与全部复数样本不能被新配置覆盖");
    }
}

void RequirePendingWhileBlocked(Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    do {
        fixture.service.Poll();
        Require(operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready,
            "新增源端口的配置不能跨越未结束的完整帧");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
}

void WaitForCurrentFrame(Fixture &fixture)
{
    WaitFor(
        [&fixture] {
            const auto snapshot = fixture.service.Snapshot();
            return snapshot.core.frame && snapshot.core.frame->configRevision == snapshot.settings.acquisitionRevision;
        },
        "等待当前采集版本的完整帧超时");
}

void ContinuousNewSourceCommitsAtFrameBoundary()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    auto retained = SParameter(2U);
    retained.name = "KeepS21";
    Require(Finish(fixture, fixture.service.DefineMeasurement(retained)).ok, "保留源端口 1 的测量需求");
    StartContinuous(fixture);
    BlockNextFrame(fixture);
    const auto before = fixture.service.Snapshot();
    const auto oldFrame = before.core.frame;
    const auto copy = *oldFrame;
    const auto operation = fixture.service.UpdateMeasurement(SParameter(2U, 2U), before.settings.revision);
    RequirePendingWhileBlocked(fixture, operation);
    const auto blocked = fixture.service.Snapshot();
    Require(blocked.settings.revision == before.settings.revision && blocked.core.frame == oldFrame &&
                blocked.settings.measurements.front().Parameter() == "S21" &&
                blocked.core.configRevision == before.core.configRevision,
        "旧完整帧结束前不能发布新定义或新物理版本");
    fixture.device->Release();
    Require(Finish(fixture, operation).ok, "完整帧边界原子切换双源采集需求");
    WaitForCurrentFrame(fixture);
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.revision == before.settings.revision + 1U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision + 1U &&
                after.core.frame->frameSeq > oldFrame->frameSeq && after.core.frame->ports.size() == 2U &&
                after.core.frame->plan.sourcePorts == std::vector<std::uint32_t>{1U, 2U} &&
                after.core.frame->plan.receiverMask == 3U,
        "新版本只增加必要源端口，保留运行身份并发布完整双源帧");
    RequireRatio(fixture, 0.125, -0.25);
    RequireUnchangedFrame(*oldFrame, copy);
}

void ContinuousPreparationFailurePreservesOldRun()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    StartContinuous(fixture);
    const auto before = fixture.service.Snapshot();
    const auto copy = *before.core.frame;
    fixture.device->FailNextPlanPreparation();
    const auto result =
        Finish(fixture, fixture.service.UpdateMeasurement(SParameter(2U, 2U), before.settings.revision));
    Require(!result.ok, "物理计划准备失败必须拒绝整个测量更新");
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.revision == before.settings.revision &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                after.core.configRevision == before.core.configRevision &&
                after.settings.measurements.front().Parameter() == "S21",
        "准备失败不提交目录或两个版本，旧连续配置仍可使用");
    WaitFor(
        [&fixture, &after] {
            return fixture.core.ReadView().frame->frameSeq > after.core.frame->frameSeq;
        },
        "准备失败后连续采集必须继续产生完整帧");
    RequireRatio(fixture, 0.25, -0.5);
    RequireUnchangedFrame(*before.core.frame, copy);
}

void ContinuousNewReceiverCommitsAtFrameBoundary()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    StartContinuous(fixture);
    BlockNextFrame(fixture);
    const auto before = fixture.service.Snapshot();
    auto receiver = SParameter(3U);
    receiver.name = "RawB3";
    receiver.kind = VnaService::MeasurementKind::RECEIVER;
    const auto operation = fixture.service.DefineMeasurement(receiver, before.settings.revision);
    RequirePendingWhileBlocked(fixture, operation);
    Require(fixture.service.Settings().revision == before.settings.revision &&
                fixture.service.Settings().measurements.size() == 1U &&
                fixture.core.ReadView().configRevision == before.core.configRevision,
        "第三接收机启用前先等待旧完整帧结束，不能提前提交目录或计划");
    fixture.device->Release();
    Require(Finish(fixture, operation).ok, "在完整帧边界启用新增第三接收机");
    WaitForCurrentFrame(fixture);
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.revision == before.settings.revision + 1U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision + 1U &&
                after.core.frame->plan.receiverMask == 7U &&
                after.core.frame->plan.sourcePorts == std::vector<std::uint32_t>{1U} &&
                after.core.frame->ports.size() == 1U && after.core.frame->frameSeq > before.core.frame->frameSeq,
        "只扩展必要接收机而不增加源端口，连续运行及完整帧序号保持连续");
    Require(fixture.service.SelectMeasurement("RawB3"), "选择新增原始第三接收机测量");
    const auto data = fixture.service.MeasurementData();
    Require(data.ok && data.values.size() == 6U, "新增接收机可以读取完整三点原始复数数据");
    for (const double value : data.values) {
        Require(std::fabs(value - 7.0) < 1e-12, "第三接收机各复数分量符合独立给定的原始样本");
    }
}

void ContinuousCatalogEditsReuseCurrentAcquisition()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    StartContinuous(fixture);
    BlockNextFrame(fixture);
    const auto before = fixture.service.Snapshot();
    auto clone = SParameter(2U);
    clone.name = "Trc2";
    Require(Finish(fixture, fixture.service.DefineMeasurement(clone, before.settings.revision)).ok,
        "连续扫描时新增相同采集需求的迹线");
    Require(fixture.service.SelectMeasurement("Trc2"), "连续扫描时选择已有迹线");
    const auto added = fixture.service.Snapshot();
    Require(added.settings.measurements.size() == 2U && added.settings.revision == before.settings.revision + 1U &&
                added.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                added.core.frame == before.core.frame,
        "新增目录项只增加逻辑版本，保留完整采集帧");
    Require(Finish(fixture, fixture.service.DeleteMeasurement("Trc2", added.settings.revision)).ok,
        "连续扫描时删除已有迹线");
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.measurements.size() == 1U && after.settings.selectedMeasurement == "Trc1" &&
                after.settings.revision == before.settings.revision + 2U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                after.core.configRevision == before.core.configRevision && after.core.frame == before.core.frame,
        "目录删除只回退选择和增加逻辑版本，不重启或清空采集");
    RequireRatio(fixture, 0.25, -0.5);
}

void ContinuousUpdatesRecheckQueuedRevisionAndOwner()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    StartContinuous(fixture);
    BlockNextFrame(fixture);
    const auto before = fixture.service.Snapshot();
    const auto first = fixture.service.UpdateMeasurement(SParameter(1U), before.settings.revision);
    const auto stale = fixture.service.UpdateMeasurement(SParameter(2U, 2U), before.settings.revision);
    Require(Finish(fixture, first).ok && !Finish(fixture, stale).ok, "执行时拒绝排队竞争造成的过期版本");
    const auto after = fixture.service.Snapshot();
    Require(!Finish(fixture, fixture.service.UpdateMeasurement(SParameter(2U), after.settings.revision, "OTHER")).ok,
        "连续扫描不降低控制权保护要求");
    Require(fixture.service.Settings().revision == before.settings.revision + 1U &&
                fixture.service.Settings().measurements.front().Parameter() == "S11" &&
                fixture.core.ReadView().configRevision == before.core.configRevision,
        "被拒绝的更新不遗留源端口、目录项或配置版本");
}

void TwoPortCapabilitiesRejectUnsupportedLiveReceiver()
{
    VnaService::InstrumentProfile profile;
    profile.portCount = 2U;
    Fixture fixture(profile);
    ClaimAndPrepare(fixture);
    StartContinuous(fixture);
    const auto before = fixture.service.Snapshot();
    auto invalid = SParameter(3U);
    invalid.kind = VnaService::MeasurementKind::RECEIVER;
    Require(!Finish(fixture, fixture.service.UpdateMeasurement(invalid, before.settings.revision)).ok,
        "双端口运行不能启用第三接收机");
    invalid.receiverPort = 1U;
    invalid.sourcePort = 3U;
    Require(!Finish(fixture, fixture.service.UpdateMeasurement(invalid, before.settings.revision)).ok,
        "双端口运行不能启用第三激励源");
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.revision == before.settings.revision &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                after.settings.measurements.front().Parameter() == "S21",
        "能力拒绝不能改变目录、逻辑版本或采集版本");
}

void ArmedManualModeChangesMeasurementWithoutTrigger()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    Require(Finish(fixture, fixture.service.SetTriggerSource(VnaService::TriggerSource::MANUAL)).ok, "准备手动触发源");
    Require(Finish(fixture, fixture.service.SetSweepMode(VnaService::SweepMode::CONTINUOUS)).ok, "布防手动连续模式");
    const auto before = fixture.service.Snapshot();
    Require(!before.core.frame, "手动布防不能自动产生初始帧");
    Require(Finish(fixture, fixture.service.UpdateMeasurement(SParameter(2U, 2U), before.settings.revision)).ok,
        "手动布防期间修改测量所需源端口");
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.triggerSource == VnaService::TriggerSource::MANUAL && !after.core.frame &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision + 1U,
        "新计划提交不能隐式触发扫描或改为内部连续触发");
    Require(Finish(fixture, fixture.service.Trigger()).ok, "新测量定义仍由显式手动触发采集");
    Require(fixture.core.ReadView().frame->frameSeq == 1U, "显式触发后才发布第一帧");
    RequireRatio(fixture, 0.125, -0.25);
}

void PhysicalSweepConfigurationStillRequiresHold()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    StartContinuous(fixture);
    const auto before = fixture.service.Snapshot();
    Require(!Finish(fixture, fixture.service.SetPoints(5U, before.settings.revision)).ok,
        "当前版本的物理扫描点数修改仍要求 HOLD");
    VnaService::ConfigPatch patch;
    patch.hasIfBandwidthHz = true;
    patch.ifBandwidthHz = 10000.0;
    Require(!Finish(fixture, fixture.service.ApplyConfiguration(patch, before.settings.revision, "SCPI")).ok,
        "测量目录放开不改变物理扫频配置的现有准入规则");
    const auto after = fixture.service.Snapshot();
    RequireContinuousRun(before, after);
    Require(after.settings.points == before.settings.points &&
                after.settings.ifBandwidthHz == before.settings.ifBandwidthHz &&
                after.settings.revision == before.settings.revision &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision,
        "物理配置拒绝不影响连续运行和任何已提交版本");
}

VnaService::Operation BeginBlockedSingle(Fixture &fixture)
{
    fixture.device->Arm();
    const auto single = fixture.service.RunSingle("SCPI");
    WaitFor(
        [&fixture] {
            fixture.service.Poll();
            return fixture.device->HasEntered();
        },
        "等待有限单次采集进入阻塞关卡超时");
    return single;
}

void MeasurementUpdateWaitsForFiniteSingleCompletion()
{
    Fixture fixture;
    ClaimAndPrepare(fixture);
    const auto single = BeginBlockedSingle(fixture);
    const auto before = fixture.service.Snapshot();
    const auto update = fixture.service.UpdateMeasurement(SParameter(2U, 2U), before.settings.revision);
    RequirePendingWhileBlocked(fixture, update);
    Require(single.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Settings().revision == before.settings.revision &&
                fixture.service.Settings().acquisitionRevision == before.settings.acquisitionRevision &&
                fixture.service.Settings().measurements.front().Parameter() == "S21",
        "有限帧结束前排队更新不能提交任何定义或版本");
    fixture.device->Release();
    Require(Finish(fixture, single).ok, "先完成旧测量定义的单次完整帧和停止屏障");
    const auto completed = fixture.service.Snapshot();
    const auto finiteFrame = completed.core.frame;
    Require(completed.settings.sweepMode == VnaService::SweepMode::HOLD &&
                completed.core.state == VnaCore::CoreState::READY && finiteFrame &&
                completed.settings.revision == before.settings.revision &&
                finiteFrame->configRevision == before.settings.acquisitionRevision &&
                finiteFrame->plan.sourcePorts == std::vector<std::uint32_t>{1U} &&
                finiteFrame->runId == before.core.runId && finiteFrame->frameSeq == 1U,
        "SING 仍按旧定义完成且返回 HOLD，目录更新在其后执行");
    Require(Finish(fixture, update).ok, "有限操作完成后提交排队的新源端口需求");
    const auto after = fixture.service.Snapshot();
    Require(after.settings.sweepMode == VnaService::SweepMode::HOLD && after.core.state == VnaCore::CoreState::READY &&
                after.core.runId == before.core.runId && after.settings.revision == before.settings.revision + 1U &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision + 1U &&
                after.core.frame == finiteFrame && !fixture.service.MeasurementData().ok,
        "排队更新保持 HOLD 和运行身份，保留旧完整帧但拒绝作为新定义当前数据读取");
}

void Run(const std::string &name, void (*test)(), unsigned &failed)
{
    try {
        test();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}
}  // 匿名命名空间

int main()
{
    unsigned failed = 0U;
    Run("已有需求即时复算同一帧", ContinuousCoveredMeasurementUsesSameFrame, failed);
    Run("新增源端口在完整帧边界提交", ContinuousNewSourceCommitsAtFrameBoundary, failed);
    Run("计划准备失败保留连续运行", ContinuousPreparationFailurePreservesOldRun, failed);
    Run("新增接收机在完整帧边界提交", ContinuousNewReceiverCommitsAtFrameBoundary, failed);
    Run("新增删除仅调整逻辑目录", ContinuousCatalogEditsReuseCurrentAcquisition, failed);
    Run("连续更新重新校验版本与控制权", ContinuousUpdatesRecheckQueuedRevisionAndOwner, failed);
    Run("双端口拒绝超出能力的实时更新", TwoPortCapabilitiesRejectUnsupportedLiveReceiver, failed);
    Run("手动布防更新不隐式触发", ArmedManualModeChangesMeasurementWithoutTrigger, failed);
    Run("物理扫频配置继续要求 HOLD", PhysicalSweepConfigurationStillRequiresHold, failed);
    Run("有限单次帧完成后才执行排队测量更新", MeasurementUpdateWaitsForFiniteSingleCompletion, failed);
    return failed == 0U ? 0 : 1;
}
