/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>

#include "support/acquisition_core_test_peer.h"
#include "support/concurrency_assertions.h"
#include "support/fixture.h"

namespace {

using namespace VnaCore;
using namespace VnaCoreTest;

constexpr std::uint64_t MAX_IDENTITY = std::numeric_limits<std::uint64_t>::max();

struct DeviceCalls final {
    std::size_t initialize = 0U;
    std::size_t applyPlan = 0U;
    std::size_t enableRf = 0U;
    std::size_t acquirePort = 0U;
};

DeviceCalls ReadCalls(ControlledDevice &device)
{
    std::lock_guard<std::mutex> guard(device.mutex_);
    DeviceCalls calls;
    calls.initialize = device.initializeCalls_;
    calls.applyPlan = device.applyPlanCalls_;
    calls.enableRf = device.enableRfCalls_;
    calls.acquirePort = device.acquisitions_;
    return calls;
}

void RequireCompleteFrame(const MeasurementFrame &frame, std::uint64_t deviceFrame)
{
    // 仅核心标识被注入到边界；受控设备的 IQ 仍使用自己的实际采集序号。
    MeasurementFrame payload = frame;
    payload.frameSeq = deviceFrame;
    RequireFrameContent(payload);
}

void TestFrameSequenceBoundary()
{
    Fixture fixture;
    const auto started = Await(fixture.core.Start(RunMode::MANUAL));
    Require(started.ok, "帧边界测试的手动启动失败");
    AcquisitionCoreTestPeer::SeedCounters(fixture.core, started.runId, started.configRevision, MAX_IDENTITY - 1U);
    const auto last = Await(fixture.core.TriggerOnce(started.runId, started.configRevision));
    const auto retained = fixture.core.ReadView().frame;
    Require(last.ok && last.frameSeq == MAX_IDENTITY && retained && retained->frameSeq == MAX_IDENTITY,
        "未发布序号达到上限的最后一帧");
    RequireCompleteFrame(*retained, 1U);
    const MeasurementFrame snapshot = *retained;
    const auto before = ReadCalls(*fixture.device);
    const auto rejected = Await(fixture.core.TriggerOnce(started.runId, started.configRevision));
    const auto faulted = fixture.core.ReadView();
    Require(!rejected.ok && rejected.frameSeq == MAX_IDENTITY && faulted.state == CoreState::FAULTED &&
                faulted.frame == retained && rejected.message.find("frame sequence exhausted") != std::string::npos,
        "帧序号耗尽后未保持末帧并进入故障状态");
    Require(ReadCalls(*fixture.device).acquirePort == before.acquirePort, "帧序号耗尽后仍调用了设备采集");
    RequireUnchanged(*retained, snapshot);
    const auto recovered = Await(fixture.core.Recover());
    const auto restarted = Await(fixture.core.Start(RunMode::MANUAL));
    Require(recovered.ok && restarted.ok && restarted.runId == started.runId + 1U && restarted.frameSeq == 0U,
        "恢复后未创建新的运行身份并重置帧序号");
    const auto fresh = Await(fixture.core.TriggerOnce(restarted.runId, restarted.configRevision));
    Require(fresh.ok && fresh.frameSeq == 1U && fixture.core.ReadView().frame->runId == restarted.runId,
        "新运行未从第一帧开始采集");
    RequireCompleteFrame(*fixture.core.ReadView().frame, 1U);
    RequireUnchanged(*retained, snapshot);
}

void TestRunIdentityBoundary()
{
    Fixture fixture;
    const auto initial = fixture.core.ReadView();
    AcquisitionCoreTestPeer::SeedCounters(fixture.core, MAX_IDENTITY - 1U, initial.configRevision, 0U);
    const auto last = Await(fixture.core.Start(RunMode::MANUAL));
    Require(last.ok && last.runId == MAX_IDENTITY, "未创建编号达到上限的最后一次运行");
    Require(Await(fixture.core.Stop(last.runId)).ok, "最后一次运行未能正常停止");
    const auto before = ReadCalls(*fixture.device);
    const auto rejected = Await(fixture.core.Start(RunMode::MANUAL));
    const auto unchanged = fixture.core.ReadView();
    Require(!rejected.ok && rejected.runId == MAX_IDENTITY && unchanged.runId == MAX_IDENTITY &&
                unchanged.state == CoreState::READY &&
                rejected.message.find("run identity exhausted") != std::string::npos,
        "运行标识耗尽后发生回绕或破坏了空闲状态");
    Require(ReadCalls(*fixture.device).enableRf == before.enableRf, "运行标识耗尽后仍启用了设备射频");
}

void TestConfigurationRevisionBoundary()
{
    Fixture fixture;
    AcquisitionCoreTestPeer::SeedCounters(fixture.core, 0U, MAX_IDENTITY - 1U, 0U);
    const auto committedPlan = MakePlan(1U);
    const auto last = Await(fixture.core.Configure(committedPlan));
    Require(last.ok && last.configRevision == MAX_IDENTITY, "未提交版本达到上限的最后一次配置");
    const auto before = ReadCalls(*fixture.device);
    const auto rejected = Await(fixture.core.Configure(MakePlan(2U)));
    const auto unchanged = fixture.core.ReadView();
    Require(!rejected.ok && rejected.configRevision == MAX_IDENTITY && unchanged.configRevision == MAX_IDENTITY &&
                unchanged.state == CoreState::READY &&
                rejected.message.find("configuration revision exhausted") != std::string::npos,
        "配置版本耗尽后发生回绕或破坏了已提交配置");
    Require(ReadCalls(*fixture.device).applyPlan == before.applyPlan, "配置版本耗尽后仍应用了候选设备配置");
    {
        std::lock_guard<std::mutex> guard(fixture.device->mutex_);
        Require(SamePlan(fixture.device->applied_, committedPlan), "配置拒绝后设备计划被候选配置覆盖");
    }
    const auto started = Await(fixture.core.Start(RunMode::MANUAL));
    Require(started.ok && Await(fixture.core.TriggerOnce(started.runId, started.configRevision)).ok,
        "配置拒绝后原有效配置无法继续采集");
    Require(SamePlan(fixture.core.ReadView().frame->plan, committedPlan), "继续采集未使用最后成功的计划");
}

void MakeFaulted(Fixture &fixture)
{
    const auto started = Await(fixture.core.Start(RunMode::MANUAL));
    Require(started.ok, "恢复边界测试的手动启动失败");
    {
        std::lock_guard<std::mutex> guard(fixture.device->mutex_);
        fixture.device->failAcquireAt_ = fixture.device->acquisitions_ + 1U;
    }
    Require(!Await(fixture.core.TriggerOnce(started.runId, started.configRevision)).ok &&
                fixture.core.ReadView().state == CoreState::FAULTED,
        "恢复边界测试未产生设备故障");
}

void TestRecoveryRevisionBoundary()
{
    Fixture fixture;
    MakeFaulted(fixture);
    const auto failed = fixture.core.ReadView();
    AcquisitionCoreTestPeer::SeedCounters(fixture.core, failed.runId, MAX_IDENTITY - 1U, 0U);
    const auto beforeRecovery = ReadCalls(*fixture.device);
    const auto last = Await(fixture.core.Recover());
    const auto afterRecovery = ReadCalls(*fixture.device);
    Require(last.ok && last.configRevision == MAX_IDENTITY && fixture.core.ReadView().state == CoreState::READY &&
                afterRecovery.initialize == beforeRecovery.initialize + 1U &&
                afterRecovery.applyPlan == beforeRecovery.applyPlan + 1U,
        "最后一次恢复未完整重新初始化并提交上限版本");
    MakeFaulted(fixture);
    const auto before = ReadCalls(*fixture.device);
    const auto retained = fixture.core.ReadView();
    const auto rejected = Await(fixture.core.Recover());
    const auto unchanged = fixture.core.ReadView();
    const auto after = ReadCalls(*fixture.device);
    Require(!rejected.ok && rejected.configRevision == MAX_IDENTITY && unchanged.configRevision == MAX_IDENTITY &&
                unchanged.state == CoreState::FAULTED && unchanged.error == retained.error &&
                unchanged.frame == retained.frame &&
                rejected.message.find("configuration revision exhausted") != std::string::npos,
        "配置版本耗尽后恢复发生回绕或清除了故障");
    Require(after.initialize == before.initialize && after.applyPlan == before.applyPlan,
        "配置版本耗尽后恢复仍调用了设备初始化或配置");
}

}  // 匿名命名空间

int main()
{
    try {
        TestFrameSequenceBoundary();
        TestRunIdentityBoundary();
        TestConfigurationRevisionBoundary();
        TestRecoveryRevisionBoundary();
        std::cout << "核心身份边界：4 组场景通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
