/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "fixture.h"

namespace {
using VnaDisplay::DisplayChange;
using VnaDisplay::DisplayChangeKind;
using VnaServiceTest::Fixture;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Wait(const std::function<bool()> &condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        Require(std::chrono::steady_clock::now() < deadline, "等待显示域验收条件超时");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    Wait([&fixture, &operation] {
        fixture.service.Poll();
        return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
    return operation.get();
}

void DisplayChangesPreserveContinuousAcquisition()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl() && Finish(fixture, service.RunContinuous("SCPI")).ok, "启动连续扫描");
    Wait([&service] {
        return service.Snapshot().core.frame != nullptr;
    });
    const auto before = service.Snapshot();
    DisplayChange format;
    format.kind = DisplayChangeKind::FORMAT;
    format.format = VnaDisplay::DisplayFormat::PHASE;
    Require(Finish(fixture, service.ApplyDisplay(format, before.display.revision)).ok, "运行中切换格式");
    const auto after = service.Snapshot();
    Require(after.settings.revision == before.settings.revision &&
                after.settings.acquisitionRevision == before.settings.acquisitionRevision &&
                after.core.runId == before.core.runId && after.core.state == VnaCore::CoreState::RUNNING,
        "显示操作不能改变测量目录、物理计划、运行轮次或连续状态");
    Require(after.display.traces.front().format == VnaDisplay::DisplayFormat::PHASE &&
                after.display.revision == before.display.revision + 1U,
        "仅显示版本变化");
    DisplayChange autoscale;
    autoscale.kind = DisplayChangeKind::AUTOSCALE;
    Require(Finish(fixture, service.ApplyDisplay(autoscale)).ok, "连续扫描执行一次 Autoscale");
    const auto fixed = service.Display().traces.front().scale;
    const auto sequence = service.Snapshot().core.frame->frameSeq;
    Wait([&service, sequence] {
        return service.Snapshot().core.frame->frameSeq > sequence + 3U;
    });
    Require(service.Display().traces.front().scale.min == fixed.min &&
                service.Display().traces.front().scale.max == fixed.max,
        "后续帧不能驱动已保存的刻度变化");
}

void DisplayCasAndOwnershipAreAtomic()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得写控制权");
    const auto revision = service.Display().revision;
    DisplayChange grid;
    grid.kind = DisplayChangeKind::LAYOUT;
    grid.layout = VnaDisplay::LayoutMode::GRID;
    const auto first = service.ApplyDisplay(grid, revision);
    DisplayChange hidden;
    hidden.kind = DisplayChangeKind::VISIBLE;
    hidden.visible = false;
    const auto stale = service.ApplyDisplay(hidden, revision);
    Require(Finish(fixture, first).ok && !Finish(fixture, stale).ok, "队列执行时再次检查显示 CAS");
    Require(service.Display().layout == VnaDisplay::LayoutMode::GRID && service.Display().traces.front().visible,
        "旧版本不能覆盖刚提交的显示设置");
    Require(!Finish(fixture, service.ApplyDisplay(hidden, service.Display().revision, "OBSERVER")).ok,
        "观察窗口不能绕过共享控制权");
    Require(service.Display().revision == revision + 1U, "拒绝请求不能增加版本");
}

void DisplayWritesWaitForFiniteFrame()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得有限帧测试控制权");
    fixture.device->Arm();
    const auto single = service.RunSingle("SCPI");
    Wait([&fixture] {
        fixture.service.Poll();
        return fixture.device->HasEntered();
    });
    const auto revision = service.Display().revision;
    DisplayChange format;
    format.kind = DisplayChangeKind::FORMAT;
    format.format = VnaDisplay::DisplayFormat::REAL;
    const auto display = service.ApplyDisplay(format, revision);
    service.Poll();
    Require(display.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                service.Display().revision == revision,
        "显示写不得越过已接受的有限采集屏障");
    fixture.device->Release();
    Require(Finish(fixture, single).ok && Finish(fixture, display).ok, "完整帧之后提交显示写");
    Require(service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                service.Display().traces.front().format == VnaDisplay::DisplayFormat::REAL,
        "屏障与显示状态均已完成");
}

void FutureFiniteTaskCannotInvalidatePriorDisplayRead()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl() && Finish(fixture, service.RunSingle("SCPI")).ok, "发布原完整帧");
    DisplayChange autoscale;
    autoscale.kind = DisplayChangeKind::AUTOSCALE;
    const auto display = service.ApplyDisplay(autoscale);
    const auto nextSingle = service.RunSingle("SCPI");
    Require(Finish(fixture, display).ok, "后排的有限采集不能反向使已排队 Autoscale 失效");
    Require(Finish(fixture, nextSingle).ok, "后续采集照常完成");
}

void MarkerUsesOriginalSamplesAndAcquisitionVersion()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl() && Finish(fixture, service.SetPoints(10001U)).ok, "配置大点数扫描");
    Require(Finish(fixture, service.RunSingle("SCPI")).ok, "发布完整原始帧");
    DisplayChange marker;
    marker.kind = DisplayChangeKind::MARKER;
    marker.marker.hasEnabled = true;
    marker.marker.enabled = true;
    marker.marker.hasSampleIndex = true;
    marker.marker.sampleIndex = 1001U;
    marker.marker.checkAcquisitionRevision = true;
    marker.marker.expectedAcquisitionRevision = service.Settings().acquisitionRevision;
    Require(Finish(fixture, service.ApplyDisplay(marker)).ok, "选择不会出现在 2001 点传输数组里的原始点");
    const auto readout = service.DisplayData();
    Require(readout.marker.available && readout.marker.sampleIndex == 1001U &&
                readout.marker.frequencyHz == service.Snapshot().core.frame->plan.frequencyHz[1001U],
        "Marker 必须按完整真实频轴定位");
    const auto revision = service.Display().revision;
    --marker.marker.expectedAcquisitionRevision;
    Require(!Finish(fixture, service.ApplyDisplay(marker)).ok && service.Display().revision == revision,
        "旧物理版本的 Marker 输入原子拒绝");
    DisplayChange step;
    step.kind = DisplayChangeKind::MARKER;
    step.marker.hasStep = true;
    step.marker.step = 1;
    Require(Finish(fixture, service.ApplyDisplay(step)).ok && service.DisplayData().marker.sampleIndex == 1002U,
        "一步移动一个原始采样点，不能移动一个抽点间距");
}

void CloneAndCenterSpanAreBackendTransactions()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得后端控制测试权限");
    const auto before = service.Snapshot();
    Require(
        Finish(fixture, service.CloneMeasurement("Trc1", before.settings.revision, before.display.revision, "SCPI")).ok,
        "后端创建和选择测量副本");
    Require(service.Settings().measurements.size() == 2U && service.Settings().measurements.back().name == "Trc2" &&
                service.Display().selectedMeasurement == "Trc2",
        "副本名称及选择只由后端决定");
    Require(
        !Finish(fixture, service.CloneMeasurement("Trc1", service.Settings().revision, before.display.revision, "SCPI"))
                .ok &&
            service.Settings().measurements.size() == 2U,
        "Clone 显示版本竞争不能留下半个目录提交");
    VnaService::ConfigPatch patch;
    patch.hasCenterHz = true;
    patch.hasSpanHz = true;
    patch.centerHz = 20e6;
    patch.spanHz = 10e6;
    Require(Finish(fixture, service.ApplyConfiguration(patch, service.Settings().revision, "SCPI")).ok &&
                service.Settings().startHz == 15e6 && service.Settings().stopHz == 25e6,
        "中心和跨度由后端原子转换为频段");
    patch.hasStartHz = true;
    patch.startHz = 10e6;
    Require(!Finish(fixture, service.ApplyConfiguration(patch, service.Settings().revision, "SCPI")).ok,
        "端点与中心跨度不能混用");
}
}  // 匿名命名空间

int main()
{
    try {
        DisplayChangesPreserveContinuousAcquisition();
        DisplayCasAndOwnershipAreAtomic();
        DisplayWritesWaitForFiniteFrame();
        FutureFiniteTaskCannotInvalidatePriorDisplayRead();
        MarkerUsesOriginalSamplesAndAcquisitionVersion();
        CloneAndCenterSpanAreBackendTransactions();
        std::cout << "PASS 共享显示域、控制权、版本与完整帧边界\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
