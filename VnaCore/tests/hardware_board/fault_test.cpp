/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>

#include "test_environment.h"
#include "test_support.h"

namespace HardwareBoardTest {
namespace {

void FailureCase(
    const std::string &library, const std::string &trace, const std::string &step, const std::string &nativeStep)
{
    TestEnvironment environment(trace, step, step == "GET" ? 5U : 1U);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    const auto plan = Plan({2U, 4U});
    auto result = Await(core.Initialize());
    VnaCore::FramePtr retained;
    if (step != "INIT") {
        Require(result.ok, result.message);
        result = Await(core.Configure(plan));
    }
    if (step != "INIT" && step != "DOWNLOAD") {
        Require(result.ok, result.message);
        result = Await(core.Start(VnaCore::RunMode::MANUAL));
    }
    if (step == "START" || step == "GET") {
        Require(result.ok, result.message);
        const auto started = result;
        result = Await(core.TriggerOnce(started.runId, started.configRevision));
        if (step == "GET") {
            Require(result.ok, result.message);
            retained = core.ReadView().frame;
            VerifyFrame(retained, plan, 1U);
            result = Await(core.TriggerOnce(started.runId, started.configRevision));
        }
    }
    Require(!result.ok && result.message.find(nativeStep + " returned 731") != std::string::npos &&
                core.ReadView().state == VnaCore::CoreState::FAULTED && core.ReadView().frame == retained,
        "hardware failure lost the numeric status/step or published a partial frame: " + step);
    const bool incompleteSweep = step == "START" || step == "GET";
    if (incompleteSweep) {
        const auto beforeRecover = environment.Events();
        const auto recovered = Await(core.Recover());
        Require(!recovered.ok && core.ReadView().state == VnaCore::CoreState::FAULTED &&
                    environment.Events() == beforeRecover && core.ReadView().frame == retained,
            "incomplete hardware sweep recovery called INIT/SET/START or changed the frame: " + step);
    }
    Require(core.Close().ok != incompleteSweep, "hardware close hid unknown scan completion: " + step);
    const auto completed = environment.Events();
    Require(!completed.empty() && completed.back() == "RF_OFF" &&
                std::find(completed.begin(), completed.end(), "UNLOAD") == completed.end(),
        "hardware error cleanup did not close RF or wrongly unloaded the driver: " + step);
    if (retained) {
        VerifyFrame(retained, plan, 1U);
    }
    environment.VerifyThreadAndEnvironment();
}

}  // 匿名命名空间

void VerifyFaults(const std::string &library, const std::string &trace)
{
    FailureCase(library, trace + "-init", "INIT", "DrvHwInit");
    FailureCase(library, trace + "-download", "DOWNLOAD", "DrvDataFrameDownload");
    FailureCase(library, trace + "-rf-on", "RF_ON", "DrvSetRfPowerOn(1)");
    FailureCase(library, trace + "-start", "START", "DrvStartSweep");
    FailureCase(library, trace + "-get", "GET", "DrvGetSweepData");
}

void VerifyUnknownStatus(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace, "DOWNLOAD", 1U, 0U, UINT32_C(4292967295));
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    const auto failed = Await(core.Configure(Plan({2U, 4U})));
    Require(!failed.ok && failed.message.find("DrvDataFrameDownload returned 4292967295") != std::string::npos &&
                failed.message.find("signed 32-bit: -2000001") != std::string::npos &&
                core.ReadView().state == VnaCore::CoreState::FAULTED && !core.ReadView().frame,
        "高位未知厂商状态必须保留调用步骤、原始无符号值与有符号数值，不能生成完整帧");
    Require(core.Close().ok, "未知组帧错误后关闭端口失败");
    environment.VerifyThreadAndEnvironment();
}

void VerifyFailedRfOff(const std::string &library, const std::string &trace)
{
    // 初始化第一次关闭射频成功，正常关闭时的第二次调用注入厂商数值错误。
    TestEnvironment environment(trace, "RF_OFF", 2U);
    const auto plan = Plan({1U, 2U, 3U, 4U});
    VnaCore::FramePtr retained;
    {
        VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
        Success(core.Initialize());
        Success(core.Configure(plan));
        const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
        Success(core.TriggerOnce(started.runId, started.configRevision));
        retained = core.ReadView().frame;
        const auto closed = core.Close();
        Require(!closed.ok && closed.message.find("DrvSetRfPowerOn(0) returned 731") != std::string::npos,
            "hardware RF-off error was hidden");
        Require(!core.Close().ok, "cached close result unexpectedly hid RF-off failure");
    }
    const auto events = environment.Events();
    Require(std::count(events.begin(), events.end(), "RF_OFF") == 2 &&
                std::find(events.begin(), events.end(), "UNLOAD") == events.end(),
        "failed RF-off still unloaded the active driver or retried on the owner thread");
    VerifyFrame(retained, plan, 1U);
    environment.VerifyThreadAndEnvironment();
}

}  // 命名空间 HardwareBoardTest
