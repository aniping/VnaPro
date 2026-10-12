/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fixture.h"
#include "vna_core/frame_layout.h"

namespace {
using VnaServiceTest::Fixture;
constexpr unsigned TEST_POINT_COUNT = 3U;
constexpr std::chrono::seconds TEST_TIMEOUT(3);

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Near(double value, double expected)
{
    Require(std::isfinite(value) && std::fabs(value - expected) < 1e-12, "复数分量不符合独立期望值");
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + TEST_TIMEOUT;
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        Require(std::chrono::steady_clock::now() < deadline, "等待服务操作超时");
        fixture.service.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get();
}

void Prepare(Fixture &fixture)
{
    Require(fixture.service.TryAcquireControl(), "取得 SCPI 控制权");
    Require(Finish(fixture, fixture.service.SetPoints(TEST_POINT_COUNT)).ok, "提交三点扫描配置");
    Require(Finish(fixture, fixture.service.DeleteMeasurement("Trc1")).ok, "移除默认 S21 迹线，独立准备测试目录");
}

VnaService::Measurement SParameter(const std::string &name, unsigned receiver, unsigned source = 1U)
{
    VnaService::Measurement measurement;
    measurement.kind = VnaService::MeasurementKind::S_PARAMETER;
    measurement.name = name;
    measurement.receiverKind = 'b';
    measurement.receiverPort = receiver;
    measurement.sourcePort = source;
    return measurement;
}

std::vector<double> KnownSamples()
{
    std::vector<double> iq(TEST_POINT_COUNT * VnaCore::IQ_VALUES_PER_POINT, 7.0);
    // 独立给出三组波量；a2=2*a1，而 b1/a1=0.5+0.25i、b2/a1=0.25-0.5i。
    const double referenceReal[] = {2.0, -3.0, 4.0};
    const double referenceImag[] = {4.0, 1.0, -2.0};
    const double reflectionReal[] = {0.0, -1.75, 2.5};
    const double reflectionImag[] = {2.5, -0.25, 0.0};
    const double transmissionReal[] = {2.5, -0.25, 0.0};
    const double transmissionImag[] = {0.0, 1.75, -2.5};
    for (unsigned point = 0; point < TEST_POINT_COUNT; ++point) {
        const std::size_t offset = point * VnaCore::IQ_VALUES_PER_POINT;
        iq[offset] = referenceReal[point];
        iq[offset + 1U] = referenceImag[point];
        iq[offset + 2U] = reflectionReal[point];
        iq[offset + 3U] = reflectionImag[point];
        iq[offset + 4U] = referenceReal[point] * 2.0;
        iq[offset + 5U] = referenceImag[point] * 2.0;
        iq[offset + 6U] = transmissionReal[point];
        iq[offset + 7U] = transmissionImag[point];
    }
    return iq;
}

void ExpectRatio(Fixture &fixture, const std::string &name, double real, double imag)
{
    Require(fixture.service.SelectMeasurement(name), "选择已有 S 参数测量");
    const auto result = fixture.service.MeasurementData();
    Require(result.ok && result.values.size() == TEST_POINT_COUNT * 2U, "完整返回三点复数数据");
    for (unsigned point = 0; point < TEST_POINT_COUNT; ++point) {
        Near(result.values[point * 2U], real);
        Near(result.values[point * 2U + 1U], imag);
    }
}

void KnownRatiosAndReferenceDemand()
{
    Fixture fixture;
    Prepare(fixture);
    fixture.device->SetSamples(KnownSamples());
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("Transmission", 2U))).ok, "仅定义 S21 测量");
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "完成 S21 扫描");
    const auto first = fixture.service.Snapshot().core.frame;
    Require(first && first->plan.receiverMask == 3U, "S21 计划同时启用目标接收机与 a1 参考接收机");
    ExpectRatio(fixture, "Transmission", 0.25, -0.5);
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("Reflection", 1U))).ok, "增加 S11 测量");
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "完成包含两个测量的单帧扫描");
    const auto frame = fixture.service.Snapshot().core.frame;
    Require(frame && frame->ports.size() == 1U && frame->ports.front().sourcePort == 1U,
        "两个 S 参数共同使用唯一的源端口 1 完整帧");
    ExpectRatio(fixture, "Transmission", 0.25, -0.5);
    ExpectRatio(fixture, "Reflection", 0.5, 0.25);
    Require(fixture.service.Snapshot().core.frame == frame, "读取和选择测量不会重新采集或拼接帧");
}

void InvalidReferenceAndSampleRejectWholeArray()
{
    Fixture fixture;
    Prepare(fixture);
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("Reflection", 1U))).ok, "定义 S11 测量");
    auto samples = KnownSamples();
    samples[VnaCore::IQ_VALUES_PER_POINT] = 1e-13;
    samples[VnaCore::IQ_VALUES_PER_POINT + 1U] = 0.0;
    fixture.device->SetSamples(samples);
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "完成含弱参考波的扫描");
    const auto smallReference = fixture.service.MeasurementData();
    Require(!smallReference.ok && smallReference.values.empty() && !smallReference.error.empty(),
        "中间点参考波过小时拒绝整个数组，不泄露之前的部分点");
    samples = KnownSamples();
    samples[2U] = std::numeric_limits<double>::quiet_NaN();
    fixture.device->SetSamples(samples);
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "完成含无效接收波的扫描");
    const auto invalid = fixture.service.MeasurementData();
    Require(!invalid.ok && invalid.values.empty() && !invalid.error.empty(), "无效接收波不能成为有效 S 参数");
}

void ReverseRatiosAndCompleteTwoSourceFrame()
{
    Fixture fixture;
    Prepare(fixture);
    fixture.device->SetSamples(KnownSamples());
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("ReverseReflection", 2U, 2U))).ok,
        "定义仅使用源端口 2 的 S22");
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "完成 S22 扫描");
    const auto reverse = fixture.service.Snapshot().core.frame;
    Require(reverse && reverse->plan.receiverMask == 2U && reverse->plan.sourcePorts == std::vector<std::uint32_t>{2U},
        "S22 只启用端口 2 的激励和参考接收器");
    ExpectRatio(fixture, "ReverseReflection", 0.125, -0.25);
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("ReverseTransmission", 1U, 2U))).ok,
        "增加 S12 测量");
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("Reflection", 1U))).ok, "增加 S11 测量");
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("Transmission", 2U))).ok, "增加 S21 测量");
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "完成包含全部四参数的双源轮次");
    const auto frame = fixture.service.Snapshot().core.frame;
    Require(frame && frame->ports.size() == 2U && frame->plan.sourcePorts == std::vector<std::uint32_t>{1U, 2U} &&
                frame->plan.receiverMask == 3U,
        "四参数应共用完整的双源帧和两路参考接收器");
    ExpectRatio(fixture, "Reflection", 0.5, 0.25);
    ExpectRatio(fixture, "Transmission", 0.25, -0.5);
    ExpectRatio(fixture, "ReverseTransmission", 0.25, 0.125);
    ExpectRatio(fixture, "ReverseReflection", 0.125, -0.25);
    Require(fixture.service.Snapshot().core.frame == frame, "切换四个参数不能拼接不同轮次的数据");
}

void ConfigurationChangeRejectsRetainedFrame()
{
    Fixture fixture;
    Prepare(fixture);
    fixture.device->SetSamples(KnownSamples());
    Require(Finish(fixture, fixture.service.DefineMeasurement(SParameter("Reflection", 1U))).ok, "定义 S11 测量");
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "发布当前配置的完整帧");
    ExpectRatio(fixture, "Reflection", 0.5, 0.25);
    const auto frame = fixture.service.Snapshot().core.frame;
    Require(Finish(fixture, fixture.service.SetStopHz(110000000.0)).ok, "提交新的频率配置");
    const auto snapshot = fixture.service.Snapshot();
    Require(snapshot.core.frame == frame && frame->configRevision != snapshot.settings.acquisitionRevision,
        "保留上一帧时明确标记其配置版本已经过期");
    const auto result = fixture.service.MeasurementData();
    Require(!result.ok && result.values.empty(), "新配置采集完成前禁止返回旧帧的 S 参数");
}

void UnsupportedDefinitionAndRawCompatibility()
{
    Fixture fixture;
    Prepare(fixture);
    const auto revision = fixture.service.Settings().revision;
    Require(
        !Finish(fixture, fixture.service.DefineMeasurement(SParameter("S13", 1U, 3U))).ok, "拒绝双端口范围外的 S13");
    Require(
        !Finish(fixture, fixture.service.DefineMeasurement(SParameter("S23", 2U, 3U))).ok, "拒绝双端口范围外的 S23");
    Require(!Finish(fixture, fixture.service.DefineMeasurement(SParameter("S31", 3U))).ok, "拒绝超出首发范围的 S31");
    auto unknown = SParameter("Unknown", 1U);
    unknown.kind = static_cast<VnaService::MeasurementKind>(99);
    Require(!Finish(fixture, fixture.service.DefineMeasurement(unknown)).ok, "拒绝未知测量种类");
    Require(fixture.service.Settings().revision == revision && fixture.service.Settings().measurements.empty(),
        "拒绝的定义不改变配置版本或目录");
    VnaService::Measurement raw;
    raw.name = "RawB4";
    raw.receiverKind = 'B';
    raw.receiverPort = 4U;
    raw.sourcePort = 4U;
    Require(Finish(fixture, fixture.service.DefineMeasurement(raw)).ok, "原始接收机保留大小写兼容和四端口范围");
    Require(fixture.service.Settings().measurements.front().Parameter() == "b4,4", "规范化原始测量参数");
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "采集原始 b4,4 波量");
    const auto rawData = fixture.service.MeasurementData();
    Require(rawData.ok && rawData.values.size() == TEST_POINT_COUNT * 2U, "原始接收机交错复数读取保持兼容");
    for (double value : rawData.values) {
        Near(value, 1.11);
    }
}

void MeasurementDefinitionRespectsControlAndRevision()
{
    Fixture fixture;
    Prepare(fixture);
    const auto revision = fixture.service.Settings().revision;
    Require(!Finish(fixture, fixture.service.DefineMeasurement(SParameter("WrongRevision", 1U), revision - 1U)).ok,
        "旧版本的 S 参数定义被拒绝");
    fixture.service.ReleaseControl();
    Require(!Finish(fixture, fixture.service.DefineMeasurement(SParameter("NoOwner", 1U), revision)).ok,
        "没有控制权时不能添加 S 参数");
    Require(fixture.service.TryAcquireControl("HTTP_TEST"), "另一协议取得控制权");
    Require(!Finish(fixture, fixture.service.DefineMeasurement(SParameter("OtherOwner", 1U), revision)).ok,
        "SCPI 不能覆盖另一协议的测量目录");
    Require(
        Finish(fixture, fixture.service.DefineMeasurement(SParameter("OwnedReflection", 1U), revision, "HTTP_TEST")).ok,
        "当前所有者与正确版本共同满足才提交测量定义");
    Require(fixture.service.Settings().measurements.size() == 1U &&
                fixture.service.Settings().measurements.front().name == "OwnedReflection",
        "拒绝的竞争请求不会遗留测量项");
}

void MeasurementUpdatePreservesCatalogAndInvalidatesFrame()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(
        service.TryAcquireControl() && Finish(fixture, service.SetPoints(TEST_POINT_COUNT)).ok, "准备默认迹线更新测试");
    fixture.device->SetSamples(KnownSamples());
    Require(Finish(fixture, service.DefineMeasurement(SParameter("OtherReflection", 1U))).ok, "增加其他反射迹线");
    Require(Finish(fixture, service.RunSingle("SCPI")).ok, "更新前发布完整帧");
    ExpectRatio(fixture, "Trc1", 0.25, -0.5);
    const auto oldFrame = service.Snapshot().core.frame;
    const auto revision = service.Settings().revision;
    auto updated = SParameter("Trc1", 2U, 2U);
    updated.receiverKind = 'B';
    Require(Finish(fixture, service.UpdateMeasurement(updated, revision)).ok, "原位将默认 S21 更新为 S22");
    const auto &settings = service.Settings();
    Require(settings.revision == revision + 1U && settings.measurements.size() == 2U &&
                service.Display().selectedMeasurement == "Trc1" && settings.measurements[0].name == "Trc1" &&
                settings.measurements[0].Parameter() == "S22" && settings.measurements[0].receiverKind == 'b' &&
                settings.measurements[1].name == "OtherReflection",
        "更新仅增加一个版本，保留数量、名称、目录顺序和当前选择");
    Require(service.Snapshot().core.frame == oldFrame && oldFrame->configRevision != settings.acquisitionRevision &&
                !service.MeasurementData().ok,
        "更新定义后保留旧快照但禁止将它作为当前测量读取");
    Require(Finish(fixture, service.RunSingle("SCPI")).ok, "新定义重新完成双源采集");
    const auto frame = service.Snapshot().core.frame;
    Require(frame && frame->plan.sourcePorts == std::vector<std::uint32_t>{1U, 2U} && frame->plan.receiverMask == 3U,
        "新的 S22 计划包含源 2 的参考接收器和完整双源轮次");
    ExpectRatio(fixture, "Trc1", 0.125, -0.25);
}

void RejectedMeasurementUpdatesAreAtomic()
{
    VnaService::InstrumentProfile profile;
    profile.portCount = 2U;
    Fixture fixture(profile);
    auto &service = fixture.service;
    Require(service.TryAcquireControl() && Finish(fixture, service.SetPoints(TEST_POINT_COUNT)).ok, "准备原子拒绝测试");
    const auto revision = service.Settings().revision;
    auto invalid = SParameter("Trc1", 1U, 3U);
    invalid.kind = VnaService::MeasurementKind::RECEIVER;
    Require(!Finish(fixture, service.UpdateMeasurement(invalid, revision)).ok, "拒绝配置能力外的源端口");
    Require(!Finish(fixture, service.UpdateMeasurement(SParameter("Missing", 1U), revision)).ok,
        "不存在的名称不能被更新隐式新增");
    Require(!Finish(fixture, service.UpdateMeasurement(SParameter("Trc1", 1U), revision - 1U)).ok,
        "旧版本不能覆盖当前迹线");
    Require(!Finish(fixture, service.UpdateMeasurement(SParameter("Trc1", 1U), revision, "OTHER")).ok,
        "其他所有者不能修改测量定义");
    Require(Finish(fixture, service.RunContinuous("SCPI")).ok, "启动连续采集以验证模式约束");
    Require(
        !Finish(fixture, service.SetPoints(TEST_POINT_COUNT + 1U, revision)).ok, "非 HOLD 模式不能修改物理扫描点数");
    Require(Finish(fixture, service.SetSweepMode(VnaService::SweepMode::HOLD)).ok, "完成连续采集停止屏障");
    Require(service.Settings().revision == revision && service.Settings().measurements.size() == 1U &&
                service.Settings().measurements.front().Parameter() == "S21" &&
                service.Display().selectedMeasurement == "Trc1",
        "所有拒绝保持版本、目录和选择不变");
    fixture.device->FailNextPlanPreparation();
    Require(!Finish(fixture, service.UpdateMeasurement(SParameter("Trc1", 2U, 2U), revision)).ok,
        "设备计划准备失败必须拒绝整个更新");
    Require(service.Settings().revision == revision && service.Settings().measurements.front().Parameter() == "S21",
        "计划准备失败不提交部分定义");
}

void FullCatalogSupportsInPlaceUpdates()
{
    constexpr unsigned TEST_CATALOG_CAPACITY = 32U;
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得目录容量测试控制权");
    for (unsigned index = 1U; index < TEST_CATALOG_CAPACITY; ++index) {
        Require(Finish(fixture, service.DefineMeasurement(SParameter("Other" + std::to_string(index), 1U))).ok,
            "填满测量目录");
    }
    const auto revision = service.Settings().revision;
    Require(Finish(fixture, service.UpdateMeasurement(SParameter("Trc1", 2U, 2U), revision)).ok,
        "满目录仍能原位切换已有测量");
    Require(service.Settings().measurements.size() == TEST_CATALOG_CAPACITY &&
                service.Settings().revision == revision + 1U && service.Display().selectedMeasurement == "Trc1" &&
                service.Settings().measurements.front().Parameter() == "S22",
        "满目录更新没有先删后加的空窗或容量变化");
}

void MeasurementUpdatesRevalidateQueuedRevision()
{
    Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得队列版本测试控制权");
    const auto revision = service.Settings().revision;
    const auto first = service.UpdateMeasurement(SParameter("Trc1", 1U), revision);
    const auto stale = service.UpdateMeasurement(SParameter("Trc1", 2U, 2U), revision);
    Require(Finish(fixture, first).ok && !Finish(fixture, stale).ok, "执行时重新检查排队请求的预期版本");
    Require(service.Settings().revision == revision + 1U && service.Settings().measurements.size() == 1U &&
                service.Settings().measurements.front().Parameter() == "S11",
        "旧版本排队请求不能覆盖已经提交的定义");
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
    Run("已知复比值和参考接收需求", KnownRatiosAndReferenceDemand, failed);
    Run("反向比值及完整双源轮次", ReverseRatiosAndCompleteTwoSourceFrame, failed);
    Run("无效参考和接收样本", InvalidReferenceAndSampleRejectWholeArray, failed);
    Run("配置更新后的旧帧拒绝", ConfigurationChangeRejectsRetainedFrame, failed);
    Run("不支持的定义和原始接收机兼容", UnsupportedDefinitionAndRawCompatibility, failed);
    Run("测量目录控制权与版本竞争", MeasurementDefinitionRespectsControlAndRevision, failed);
    Run("原位更新与旧帧失效", MeasurementUpdatePreservesCatalogAndInvalidatesFrame, failed);
    Run("原位更新原子拒绝", RejectedMeasurementUpdatesAreAtomic, failed);
    Run("满目录原位更新", FullCatalogSupportsInPlaceUpdates, failed);
    Run("原位更新队列版本竞争", MeasurementUpdatesRevalidateQueuedRevision, failed);
    return failed == 0U ? 0 : 1;
}
