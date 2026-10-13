/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fixture.h"

namespace {
using VnaServiceTest::Fixture;

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
void Wait(Predicate condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        Require(std::chrono::steady_clock::now() < deadline, "整组测试等待超时");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    Wait([&] {
        fixture.service.Poll();
        return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
    return operation.get();
}

void SameConfig(const VnaService::InstrumentSettings &before, const VnaService::InstrumentSettings &after)
{
    Require(before.startHz == after.startHz && before.stopHz == after.stopHz && before.points == after.points &&
                before.ifBandwidthHz == after.ifBandwidthHz && before.triggerSource == after.triggerSource &&
                before.sweepMode == after.sweepMode && before.currentTriggerScope == after.currentTriggerScope,
        "整组替换必须保留全部扫描配置和触发设置");
}

void SameSavedState(const VnaService::InstrumentSnapshot &before, const VnaService::InstrumentSnapshot &after)
{
    SameConfig(before.settings, after.settings);
    Require(before.settings.revision == after.settings.revision &&
                before.settings.acquisitionRevision == after.settings.acquisitionRevision &&
                before.display.revision == after.display.revision &&
                before.display.selectedMeasurement == after.display.selectedMeasurement &&
                before.display.selectedDiagram == after.display.selectedDiagram &&
                before.settings.measurements.size() == after.settings.measurements.size() &&
                before.core.configRevision == after.core.configRevision && before.core.runId == after.core.runId,
        "失败整组操作不得修改目录、显示版本、活动选择或采集身份");
    for (std::size_t index = 0U; index < before.settings.measurements.size(); ++index) {
        Require(before.settings.measurements[index].name == after.settings.measurements[index].name &&
                    before.settings.measurements[index].Parameter() == after.settings.measurements[index].Parameter(),
            "失败候选不能替换旧测量定义");
    }
}

void Seed(Fixture &fixture)
{
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得整组测试控制权");
    VnaService::ConfigPatch config;
    config.hasStartHz = true;
    config.startHz = 20e6;
    config.hasStopHz = true;
    config.stopHz = 50e6;
    config.hasPoints = true;
    config.points = 7U;
    config.hasIfBandwidthHz = true;
    config.ifBandwidthHz = 10000.0;
    Require(Finish(fixture, service.ApplyConfiguration(config, service.Settings().revision, "SCPI")).ok,
        "准备独立扫描配置");
    VnaService::Measurement raw;
    raw.name = "OldRaw";
    Require(Finish(fixture, service.DefineMeasurement(raw)).ok, "准备待删除原始测量定义");
    Require(Finish(fixture, service.RunSingle("SCPI")).ok, "准备有效完整帧");
    VnaDisplay::DisplayChange format;
    format.kind = VnaDisplay::DisplayChangeKind::FORMAT;
    format.name = "Trc1";
    format.format = VnaDisplay::DisplayFormat::PHASE;
    Require(Finish(fixture, service.ApplyDisplay(format)).ok, "准备旧同名迹线格式");
    VnaDisplay::DisplayChange marker;
    marker.kind = VnaDisplay::DisplayChangeKind::MARKER;
    marker.name = "Trc1";
    marker.marker.action = VnaDisplay::MarkerAction::ADD;
    Require(Finish(fixture, service.ApplyDisplay(marker)).ok, "准备旧同名迹线标记");
}

void MatrixHasFreshDefinitionsAndDisplay(unsigned ports)
{
    VnaService::InstrumentProfile profile;
    profile.portCount = ports;
    Fixture fixture(profile);
    Seed(fixture);
    const auto before = fixture.service.Snapshot();
    for (unsigned repeat = 0U; repeat < 2U; ++repeat) {
        auto &service = fixture.service;
        const auto displayRevision = service.Display().revision;
        Require(Finish(fixture, service.CreateAllSParameters(service.Settings().revision, displayRevision, "SCPI")).ok,
            "执行已确认的整组替换");
        const auto after = service.Snapshot();
        SameConfig(before.settings, after.settings);
        Require(after.settings.revision == before.settings.revision + repeat + 1U &&
                    after.display.revision == displayRevision + (repeat == 0U ? 1U : 0U) &&
                    after.settings.measurements.size() == ports * ports &&
                    after.display.diagrams.size() == ports * ports && after.display.traces.size() == ports * ports &&
                    after.display.selectedMeasurement == "Trc1" && after.display.selectedDiagram == 1U &&
                    after.display.gridColumns == ports,
            "整组必须精确替换 n² 定义；重复默认矩阵不能累加图窗或无故改变显示版本");
        for (unsigned index = 0U; index < ports * ports; ++index) {
            const auto name = "Trc" + std::to_string(index + 1U);
            const auto parameter = "S" + std::to_string(index / ports + 1U) + std::to_string(index % ports + 1U);
            const auto expectedFormat =
                index / ports == index % ports ? VnaDisplay::DisplayFormat::SMITH : VnaDisplay::DisplayFormat::DB;
            Require(after.settings.measurements[index].name == name &&
                        after.settings.measurements[index].Parameter() == parameter &&
                        after.display.diagrams[index].measurementNames == std::vector<std::string>{name} &&
                        after.display.traces[index].format == expectedFormat &&
                        after.display.traces[index].cartesianFormat == VnaDisplay::DisplayFormat::DB &&
                        after.display.traces[index].smithRadius == 1.0 &&
                        std::none_of(after.display.traces[index].markers.Items().begin(),
                            after.display.traces[index].markers.Items().end(),
                            [](const VnaDisplay::MarkerSettings &marker) {
                                return marker.enabled;
                            }),
                "每项应按矩阵顺序使用独立图窗和默认显示，不继承旧同名标记");
        }
    }
}

void RejectionsAndPlanFailureAreAtomic()
{
    Fixture fixture;
    Seed(fixture);
    auto &service = fixture.service;
    const auto before = service.Snapshot();
    for (const auto &operation :
        {service.CreateAllSParameters(before.settings.revision - 1U, before.display.revision, "SCPI"),
            service.CreateAllSParameters(before.settings.revision, before.display.revision - 1U, "SCPI"),
            service.CreateAllSParameters(0U, before.display.revision, "SCPI"),
            service.CreateAllSParameters(before.settings.revision, 0U, "SCPI"),
            service.CreateAllSParameters(before.settings.revision, before.display.revision, "OBSERVER")}) {
        Require(!Finish(fixture, operation).ok, "过期版本或其他控制者必须拒绝");
        SameSavedState(before, service.Snapshot());
    }
    fixture.device->FailNextPlanPreparation();
    Require(
        !Finish(fixture, service.CreateAllSParameters(before.settings.revision, before.display.revision, "SCPI")).ok,
        "设备计划准备失败必须拒绝整组候选");
    SameSavedState(before, service.Snapshot());
}

void ContinuousMatrixCommitsAtCompleteFrameBoundary()
{
    Fixture fixture;
    Seed(fixture);
    auto &service = fixture.service;
    Require(Finish(fixture, service.RunContinuous("SCPI")).ok, "启动连续扫描");
    fixture.device->Arm();
    Wait([&] {
        service.Poll();
        return fixture.device->HasEntered();
    });
    const auto before = service.Snapshot();
    const auto operation = service.CreateAllSParameters(before.settings.revision, before.display.revision, "SCPI");
    for (unsigned poll = 0U; poll < 5U; ++poll) {
        service.Poll();
        Require(operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready,
            "新增源需求不能越过阻塞的完整帧边界");
        SameSavedState(before, service.Snapshot());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fixture.device->Release();
    Require(Finish(fixture, operation).ok, "完整帧后提交新矩阵");
    const auto after = service.Snapshot();
    SameConfig(before.settings, after.settings);
    Require(after.core.state == VnaCore::CoreState::RUNNING && after.core.runId == before.core.runId &&
                after.settings.acquisitionRevision > before.settings.acquisitionRevision,
        "整组替换不能停止或重启 CONT，仅增加缺失源需求");
}

void FullCatalogReplacementAndQueuedConflict()
{
    Fixture fixture;
    Seed(fixture);
    auto &service = fixture.service;
    while (service.Settings().measurements.size() < VnaDisplay::MAX_DISPLAY_TRACES) {
        VnaService::Measurement measurement;
        measurement.name = "Legacy" + std::to_string(service.Settings().measurements.size());
        Require(Finish(fixture, service.DefineMeasurement(measurement)).ok, "准备满容量旧目录");
    }
    Require(
        Finish(fixture, service.CreateAllSParameters(service.Settings().revision, service.Display().revision, "SCPI"))
                .ok &&
            service.Settings().measurements.size() == 16U,
        "整组替换应清理满旧目录，不按追加容量拒绝");
    fixture.device->Arm();
    const auto finite = service.RunSingle("SCPI");
    Wait([&] {
        service.Poll();
        return fixture.device->HasEntered();
    });
    const auto before = service.Snapshot();
    std::vector<VnaService::Operation> queued;
    for (unsigned index = 0U; index < 31U; ++index) {
        queued.push_back(service.CreateAllSParameters(before.settings.revision, before.display.revision, "SCPI"));
    }
    const auto overflow = service.CreateAllSParameters(before.settings.revision, before.display.revision, "SCPI");
    Require(overflow.wait_for(std::chrono::seconds(0)) == std::future_status::ready && !overflow.get().ok &&
                overflow.get().errorCode == -363,
        "队列背压必须立即拒绝额外整组任务");
    SameSavedState(before, service.Snapshot());
    fixture.device->Release();
    Require(Finish(fixture, finite).ok && Finish(fixture, queued.front()).ok, "前置帧及首个整组任务应完成");
    for (std::size_t index = 1U; index < queued.size(); ++index) {
        Require(!Finish(fixture, queued[index]).ok, "同一旧双版本只能提交一个整组操作");
    }
    Require(
        service.Settings().measurements.size() == 16U && service.Settings().revision == before.settings.revision + 1U,
        "竞争失败不得重复创建或累加目录");
}

void FaultedCoreAndContinuousPlanFailureAreAtomic()
{
    Fixture faulted;
    Seed(faulted);
    faulted.device->Arm(true);
    const auto finite = faulted.service.RunSingle("SCPI");
    Wait([&] {
        faulted.service.Poll();
        return faulted.device->HasEntered();
    });
    faulted.device->Release();
    Require(!Finish(faulted, finite).ok, "准备已故障核心");
    const auto beforeFault = faulted.service.Snapshot();
    Require(!Finish(faulted, faulted.service.CreateAllSParameters(
                                 beforeFault.settings.revision, beforeFault.display.revision, "SCPI"))
                 .ok,
        "故障核心不能替换旧场景");
    SameSavedState(beforeFault, faulted.service.Snapshot());
    Fixture continuous;
    Seed(continuous);
    Require(Finish(continuous, continuous.service.RunContinuous("SCPI")).ok, "准备连续扫描失败场景");
    continuous.device->Arm();
    Wait([&] {
        continuous.service.Poll();
        return continuous.device->HasEntered();
    });
    const auto before = continuous.service.Snapshot();
    continuous.device->FailNextPlanPreparation();
    const auto operation =
        continuous.service.CreateAllSParameters(before.settings.revision, before.display.revision, "SCPI");
    continuous.service.Poll();
    continuous.device->Release();
    Require(!Finish(continuous, operation).ok, "连续计划准备失败应拒绝整个候选");
    SameSavedState(before, continuous.service.Snapshot());
    Require(
        continuous.core.ReadView().state == VnaCore::CoreState::RUNNING, "可恢复的计划准备失败不能中断原 CONT 扫描");
}

void ManualArmedMatrixDoesNotTrigger()
{
    Fixture fixture;
    Seed(fixture);
    auto &service = fixture.service;
    Require(service.SetCurrentTriggerScope(true) &&
                Finish(fixture, service.SetTriggerSource(VnaService::TriggerSource::MANUAL)).ok &&
                Finish(fixture, service.SetSweepMode(VnaService::SweepMode::CONTINUOUS)).ok,
        "准备手动触发布防和非默认触发范围");
    const auto before = service.Snapshot();
    Require(Finish(fixture, service.CreateAllSParameters(before.settings.revision, before.display.revision, "SCPI")).ok,
        "手动布防时允许整组替换");
    const auto after = service.Snapshot();
    SameConfig(before.settings, after.settings);
    Require(after.core.state == VnaCore::CoreState::RUNNING && after.core.runId == before.core.runId &&
                after.core.frame == before.core.frame,
        "整组替换不得改变触发源、触发范围或隐式启动采集");
}
}  // 匿名命名空间

int main()
{
    try {
        MatrixHasFreshDefinitionsAndDisplay(2U);
        MatrixHasFreshDefinitionsAndDisplay(4U);
        RejectionsAndPlanFailureAreAtomic();
        ContinuousMatrixCommitsAtCompleteFrameBoundary();
        FullCatalogReplacementAndQueuedConflict();
        FaultedCoreAndContinuousPlanFailureAreAtomic();
        ManualArmedMatrixDoesNotTrigger();
        std::cout << "PASS 整组替换、扫描连续性、双版本与背压\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
