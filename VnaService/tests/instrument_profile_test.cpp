/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"

namespace {
void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaService::OperationResult Finish(VnaServiceTest::Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        Require(std::chrono::steady_clock::now() < deadline, "等待能力配置操作超时");
        fixture.service.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get();
}

VnaService::ConfigPatch Range(double start, double stop)
{
    VnaService::ConfigPatch patch;
    patch.hasStartHz = true;
    patch.hasStopHz = true;
    patch.startHz = start;
    patch.stopHz = stop;
    return patch;
}

void DefaultMeasurementIsUncalibratedSParameter()
{
    for (unsigned portCount : {1U, 2U, 4U}) {
        VnaService::InstrumentProfile profile;
        profile.portCount = portCount;
        VnaServiceTest::Fixture fixture(profile);
        const auto snapshot = fixture.service.Snapshot();
        const auto &settings = snapshot.settings;
        Require(settings.measurements.size() == 1U && snapshot.display.selectedMeasurement == "Trc1",
            "全新仪器应只创建并选择第一条标准 S 参数迹线");
        const auto &measurement = settings.measurements.front();
        const unsigned receiverPort = portCount >= 2U ? 2U : 1U;
        const std::string parameter = portCount >= 2U ? "S21" : "S11";
        Require(measurement.name == "Trc1" && measurement.kind == VnaService::MeasurementKind::S_PARAMETER &&
                    measurement.Parameter() == parameter && measurement.receiverKind == 'b' &&
                    measurement.receiverPort == receiverPort && measurement.sourcePort == 1U,
            "二端口及以上默认 S21，单端口回退 S11，不能显示原始入射波 a1,1");
        Require(settings.sweepMode == VnaService::SweepMode::HOLD && !snapshot.core.frame,
            "默认迹线不应自动启动采集或构造初始数据");
        Require(fixture.service.TryAcquireControl() && Finish(fixture, fixture.service.RunSingle("SCPI")).ok,
            "取得控制权后采集默认 S 参数迹线");
        const auto data = fixture.service.MeasurementData();
        Require(data.ok && data.values.size() == profile.defaultPoints * 2U, "默认测量完整返回未校准复比值");
        // 受控设备独立给出的 a1 与所有 b 波均为 1.11+1.11i，其比值必须是 1+0i。
        for (std::size_t index = 0U; index < data.values.size(); index += 2U) {
            Require(data.values[index] == 1.0 && data.values[index + 1U] == 0.0,
                "默认 S 参数必须经过复比值计算，不能直接返回原始接收波");
        }
    }
}

void HardwareProfileKeepsDeviceIdentity()
{
    const std::string identity = "Manufacturer,TDC1VN10,SERIAL,1.0";
    auto device = std::make_unique<VnaServiceTest::ControlledDevice>(identity);
    VnaCore::AcquisitionCore core(std::move(device));
    VnaService::InstrumentService service(core, VnaService::InstrumentProfile{}, false);
    std::string error;
    Require(!service.IsSimulator() && service.Identity() == "VnaPro,UnverifiedBackend,0,1.0",
        "真实驱动类型应冻结，配置未就绪前保留未验证身份");
    Require(!service.BootstrapProfile(error), "核心未初始化时必须拒绝仪器配置");
    Require(core.Initialize().get().ok, "初始化真实身份的受控设备");
    Require(service.BootstrapProfile(error), "通用仪器配置不能限制 NativeSimulator 身份前缀");
    Require(service.Identity() == identity && !service.IsSimulator(), "真实设备身份必须原样读取核心缓存");
    const auto revision = service.Settings().revision;
    Require(!service.BootstrapProfile(error) && service.Settings().revision == revision &&
                service.Settings().measurements.size() == 1U,
        "重复初始化仪器配置必须拒绝且保留原目录");
}

void RawPowerIsFrozenAcrossPlanUpdates()
{
    VnaService::InstrumentProfile profile;
    profile.defaultPoints = 3U;
    profile.defaultPowerRaw = 0xFEDCBA9876543210ULL;
    const auto expectedPower = profile.defaultPowerRaw;
    VnaServiceTest::Fixture fixture(profile);
    profile.defaultPowerRaw = 7U;
    auto &service = fixture.service;
    Require(
        service.TryAcquireControl() && Finish(fixture, service.RunSingle("SCPI")).ok, "显式原始功率配置完成首次采集");
    const auto first = service.Snapshot().core.frame;
    Require(first && first->plan.powerRaw.size() == 3U &&
                std::all_of(first->plan.powerRaw.begin(), first->plan.powerRaw.end(),
                    [expectedPower](std::uint64_t value) {
                        return value == expectedPower;
                    }),
        "逐点功率必须保留完整64位编码，不能截断或暗中解释为 dBm");
    Require(Finish(fixture, service.SetPoints(5U)).ok && Finish(fixture, service.RunSingle("SCPI")).ok,
        "修改点数后使用冻结的原始功率重新采集");
    const auto updated = service.Snapshot().core.frame;
    Require(updated && updated->plan.powerRaw.size() == 5U &&
                std::all_of(updated->plan.powerRaw.begin(), updated->plan.powerRaw.end(),
                    [expectedPower](std::uint64_t value) {
                        return value == expectedPower;
                    }) &&
                first->plan.powerRaw.size() == 3U,
        "新增频点沿用启动功率编码，外部 profile 变更和新扫描不得修改旧完整帧");
}

void StimulusAxisUsesCommittedConfiguration()
{
    VnaService::InstrumentProfile profile;
    profile.defaultPoints = 3U;
    VnaServiceTest::Fixture fixture(profile);
    auto &service = fixture.service;
    const auto original = service.StimulusAxis();
    Require(original.ok && original.values.size() == 3U && !service.Snapshot().core.frame,
        "初始配置成功后无需采集即可查询刺激轴");
    Require(service.TryAcquireControl(), "取得刺激轴配置测试控制权");
    fixture.device->ArmPlan();
    const auto update = service.SetPoints(5U);
    service.Poll();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasPlanEntered()) {
        Require(std::chrono::steady_clock::now() < deadline, "等待物理计划下发关卡超时");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto pending = service.StimulusAxis();
    Require(pending.ok && pending.values == original.values && service.Settings().points == 3U,
        "候选计划尚未下发成功时必须继续查询上一次提交的刺激轴");
    fixture.device->Release();
    Require(Finish(fixture, update).ok, "提交新五点刺激轴");
    const auto committed = service.StimulusAxis();
    Require(committed.ok && committed.values.size() == 5U && committed.values.front() == original.values.front() &&
                committed.values.back() == original.values.back() && !service.Snapshot().core.frame,
        "物理计划成功后刺激轴立即更新，不等待扫描结果");
    const auto revision = service.Settings().revision;
    fixture.device->FailNextPlanPreparation();
    Require(!Finish(fixture, service.SetPoints(7U)).ok && service.Settings().revision == revision &&
                service.StimulusAxis().values == committed.values,
        "物理计划准备失败必须保留已提交的刺激轴和版本");
    const auto invalid = Finish(fixture, service.SetStartHz(profile.minFrequencyHz - 1.0));
    Require(!invalid.ok && invalid.message.find("instrument range") != std::string::npos &&
                service.Settings().revision == revision && service.StimulusAxis().values == committed.values,
        "替换模拟器错误文案后仍拒绝越界频率，不修改已提交的刺激轴");
}

void CoupledEndpointsAreAtomic()
{
    VnaServiceTest::Fixture fixture(VnaService::InstrumentProfile{});
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得测试控制权");
    Require(Finish(fixture, service.ApplyConfiguration(Range(7e9, 7.5e9), service.Settings().revision, "SCPI")).ok,
        "设置高频扫描窗口");
    const auto highRevision = service.Settings().revision;
    Require(!Finish(fixture, service.SetStartHz(7.8e9)).ok && service.Settings().revision == highRevision &&
                service.Settings().startHz == 7e9 && service.Settings().stopHz == 7.5e9,
        "输入端点合法但联动超过8GHz时拒绝整个候选");
    Require(Finish(fixture, service.ApplyConfiguration(Range(1e8, 6e8), highRevision, "SCPI")).ok, "设置低频扫描窗口");
    const auto lowRevision = service.Settings().revision;
    Require(!Finish(fixture, service.SetStopHz(5e7)).ok && service.Settings().revision == lowRevision &&
                service.Settings().startHz == 1e8 && service.Settings().stopHz == 6e8,
        "联动低于10MHz时保留原配置和版本");
    Require(Finish(fixture, service.ApplyConfiguration(Range(1e7, 8e9), lowRevision, "SCPI")).ok,
        "支持范围的两个端点都可取到");
    Require(Finish(fixture, service.RunSingle("SCPI")).ok, "默认全频段执行一次完整采集");
    const auto frame = service.Snapshot().core.frame;
    Require(frame && frame->plan.frequencyRaw.front() == 10000000ULL &&
                frame->plan.frequencyRaw.back() == 8000000000ULL && frame->plan.frequencyHz.size() == 201U,
        "扫描计划以64位完整传递8GHz并保留201点");
}

void PortLimitsAndSinglePort()
{
    VnaService::InstrumentProfile profile;
    profile.portCount = 1U;
    VnaServiceTest::Fixture fixture(profile);
    auto &service = fixture.service;
    Require(service.IsSimulator() && service.Identity() == "VnaPro,NativeSimulator,1PORT,1.0" &&
                service.TryAcquireControl(),
        "仪器身份和控制能力符合启动配置");
    VnaService::Measurement measurement;
    measurement.name = "S11";
    measurement.kind = VnaService::MeasurementKind::S_PARAMETER;
    measurement.receiverKind = 'b';
    Require(Finish(fixture, service.DefineMeasurement(measurement)).ok, "单端口允许S11");
    const auto revision = service.Settings().revision;
    measurement.name = "S12";
    measurement.sourcePort = 2U;
    Require(!Finish(fixture, service.DefineMeasurement(measurement)).ok && service.Settings().revision == revision,
        "单端口拒绝第二激励源且保持配置");
    measurement.kind = VnaService::MeasurementKind::RECEIVER;
    measurement.sourcePort = 1U;
    measurement.receiverPort = 2U;
    Require(!Finish(fixture, service.DefineMeasurement(measurement)).ok && service.Settings().revision == revision,
        "原始接收波也必须遵守端口能力");
}
}  // 匿名命名空间

int main()
{
    try {
        DefaultMeasurementIsUncalibratedSParameter();
        HardwareProfileKeepsDeviceIdentity();
        RawPowerIsFrozenAcrossPlanUpdates();
        StimulusAxisUsesCommittedConfiguration();
        CoupledEndpointsAreAtomic();
        PortLimitsAndSinglePort();
        std::cout << "PASS 仪器边界与原子配置\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
