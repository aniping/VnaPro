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
    Require(service.Identity() == "VnaPro,NativeSimulator,1PORT,1.0" && service.TryAcquireControl(),
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
        CoupledEndpointsAreAtomic();
        PortLimitsAndSinglePort();
        std::cout << "PASS 仪器边界与原子配置\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
