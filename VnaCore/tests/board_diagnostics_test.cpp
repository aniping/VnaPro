/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "board_diagnostics_support.h"
#include "hardware_board/test_environment.h"
#include "hardware_board/test_support.h"

namespace BoardDiagnosticsTest {
namespace {
using HardwareBoardTest::Await;
using HardwareBoardTest::Plan;
using HardwareBoardTest::Require;
using HardwareBoardTest::Success;
using HardwareBoardTest::TestEnvironment;

std::vector<Summary> Summaries(const VnaCore::SweepPlan &plan)
{
    std::vector<double> ports;
    for (auto port : plan.sourcePorts) {
        ports.push_back(port - 1U);
    }
    return {{"frequency", "uint64_t", "Hz", {plan.frequencyRaw.begin(), plan.frequencyRaw.end()}},
        {"power", "double", "dBm", plan.powerDbm},
        {"ifbw", "uint32_t", "Hz", std::vector<double>(plan.frequencyRaw.size(), plan.ifBandwidthRaw)},
        {"ports", "uint32_t", "zero_based", ports}};
}

void VerifyCalls(
    const Records &records, const std::string &phase, std::uint64_t config, const std::vector<std::string> &steps)
{
    std::size_t previous = 0U;
    bool first = true;
    for (const auto &step : steps) {
        const std::vector<std::string> scope{
            "phase=" + phase, "config=" + std::to_string(config) + " ", "step=" + step + " "};
        auto beginScope = scope;
        beginScope.push_back("event=begin");
        const auto begin = Find(records, beginScope);
        auto returnedScope = scope;
        returnedScope.push_back("event=returned");
        const auto returned = Find(records, returnedScope);
        Require(begin < returned && (first || previous < begin), "Diagnostic call lifecycle order changed");
        if (step.find("List") != std::string::npos || step == "DrvSetWorkPorts") {
            Require(Field(records[returned], "status") == "void" &&
                        records[returned].find("status_u32=") == std::string::npos,
                "A void setter must not claim a successful status code");
        } else {
            VerifyStatus(records[returned], 0U);
        }
        first = false;
        previous = returned;
    }
}

void VerifyInfoAndCallSequence(const std::string &library, const std::string &prefix)
{
    Capture capture(prefix + "-info.log", VnaLog::Level::INFO);
    TestEnvironment environment(prefix + "-info.trace");
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    const auto plan = Plan({2U, 4U});
    Success(core.Configure(plan));
    auto records = capture.Read();
    Require(!records.empty(), "Required [VN10] initialization/configuration diagnostics are missing");
    Find(records, {"phase=initialize", "driver_path=" + library,
                      "abi=frequency:uint64_t,power:double,ifbw:uint32_t,ports:uint32_t"});
    VerifyCalls(records, "initialize", 0U, {"DrvHwInit", "DrvSetRfPowerOn(0)"});
    const std::vector<std::string> setters{
        "DrvSetFreqList", "DrvSetPowerList", "DrvSetIfBwList", "DrvSetWorkPorts", "DrvDataFrameDownload"};
    VerifyCalls(records, "configure", 1U, setters);
    Require(std::count_if(records.begin(), records.end(),
                [](const std::string &record) {
                    return record.find("config=1 ") != std::string::npos &&
                           record.find("event=summary") != std::string::npos;
                }) == 4,
        "INFO must publish exactly four typed configuration summaries");
    for (const auto &summary : Summaries(plan)) {
        VerifySummary(records, 1U, summary);
    }
    Require(std::none_of(records.begin(), records.end(),
                [](const std::string &record) {
                    return record.find("event=values") != std::string::npos;
                }),
        "INFO success must keep full arrays out of normal records");
    Success(core.Configure(plan));
    records = capture.Read();
    VerifyCalls(records, "configure", 2U, setters);
    Require(core.Close().ok, "Diagnostic INFO core close failed");
}

void VerifyDebugAndQuietAcquisition(const std::string &library, const std::string &prefix)
{
    Capture capture(prefix + "-debug.log", VnaLog::Level::DEBUG);
    TestEnvironment environment(prefix + "-debug.trace", HardwareBoardTest::ExpectedPower::FRACTIONAL);
    auto plan = Plan({2U, 4U});
    for (std::size_t point = 0U; point < plan.powerDbm.size(); ++point) {
        plan.powerDbm[point] = point % 2U == 0U ? -17.5 : 0.25;
    }
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    const auto configured = Success(core.Configure(plan));
    const auto records = capture.Read();
    for (const auto &summary : Summaries(plan)) {
        VerifyValues(records, 1U, summary);
    }
    const auto download = Find(records, {"config=1 ", "step=DrvDataFrameDownload ", "event=begin"});
    Require(Find(records, {"event=values", "name=frequency ", "offset=128 ", "count=2 "}) < download,
        "Last partial parameter block must be logged before download");
    const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto beforeFrames = capture.Read();
    Success(core.TriggerOnce(started.runId, configured.configRevision));
    Success(core.TriggerOnce(started.runId, configured.configRevision));
    Require(capture.Read() == beforeFrames, "Native Start/Get hot path added VN10 diagnostics per acquired frame");
    Require(core.Close().ok, "Diagnostic DEBUG core close failed");
}

void VerifyFailureDumpAndRetainedFrame(const std::string &library, const std::string &prefix, std::uint32_t failureCode)
{
    Capture capture(prefix + "-error.log", VnaLog::Level::ERROR_LEVEL);
    TestEnvironment environment(prefix + "-error.trace", "DOWNLOAD", 2U, 0U, failureCode);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    const auto plan = Plan({2U, 4U});
    const auto configured = Success(core.Configure(plan));
    const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
    Success(core.TriggerOnce(started.runId, configured.configRevision));
    Success(core.Stop(started.runId));
    const auto retained = core.ReadView().frame;
    auto candidate = plan;
    candidate.sourcePorts = {4U, 2U};
    const auto failed = Await(core.Configure(candidate));
    const auto view = core.ReadView();
    Require(!failed.ok && view.state == VnaCore::CoreState::FAULTED && view.frame == retained &&
                view.configRevision == configured.configRevision && retained->plan.sourcePorts == plan.sourcePorts,
        "A failed parameter download must preserve the last plan version and full frame");
    const auto records = capture.Read();
    const auto result = Find(records, {"phase=configure", "config=2 ", "status_u32=" + std::to_string(failureCode)});
    VerifyStatus(records[result], failureCode);
    Require(result < Find(records, {"config=2 ", "event=values", "name=frequency "}),
        "Native failure code must precede the complete failure dump");
    for (const auto &summary : Summaries(candidate)) {
        VerifyValues(records, 2U, summary);
    }
    const auto events = environment.Events();
    Require(!Await(core.Start(VnaCore::RunMode::MANUAL)).ok && environment.Events() == events,
        "Diagnostic failure reporting must not start hardware after download failure");
    Require(core.Close().ok, "Diagnostic ERROR core close failed");
}

void VerifyOffHasNoRecords(const std::string &library, const std::string &prefix)
{
    Capture capture(prefix + "-off.log", VnaLog::Level::OFF);
    TestEnvironment environment(prefix + "-off.trace");
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    const auto configured = Success(core.Configure(Plan({2U, 4U})));
    const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
    Success(core.TriggerOnce(started.runId, configured.configRevision));
    Success(core.TriggerOnce(started.runId, configured.configRevision));
    Require(core.Close().ok, "Diagnostic OFF core close failed");
    Require(capture.Read().empty(), "OFF emitted VN10 records");
}

void VerifyBindingFailure(const std::string &prefix)
{
    Capture capture(prefix + "-binding.log", VnaLog::Level::ERROR_LEVEL);
    const auto path = prefix + "-missing-library.dll";
    VnaCore::AcquisitionCore core(path, VnaCore::DriverApi::TDC1VN10);
    Require(!Await(core.Initialize()).ok, "Missing probe library unexpectedly initialized");
    const auto records = capture.Read();
    const auto &failure = records[Find(
        records, {"phase=initialize", "config=0 ", "step=BindLibrary ", "event=failed", "driver_path=" + path})];
    Require(!Field(failure, "error").empty(), "Library binding failure lost its diagnostic error text");
    Require(core.Close().ok, "Binding failure cleanup failed");
}

void VerifyNativeInitializationFailure(const std::string &library, const std::string &prefix)
{
    Capture capture(prefix + "-initialize-error.log", VnaLog::Level::ERROR_LEVEL);
    const auto failureCode = (std::numeric_limits<std::uint32_t>::max)();
    TestEnvironment environment(prefix + "-initialize-error.trace", "INIT", 1U, 0U, failureCode);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Require(!Await(core.Initialize()).ok, "Injected native initialization failure was lost");
    const auto records = capture.Read();
    const auto returned = Find(records, {"phase=initialize", "config=0 ", "step=DrvHwInit ", "event=returned"});
    VerifyStatus(records[returned], failureCode);
    Require(core.Close().ok, "Native initialization failure cleanup failed");
}
}  // 匿名命名空间
}  // 命名空间 BoardDiagnosticsTest

int main(int argc, char **argv)
{
    try {
        HardwareBoardTest::Require(argc == 3, "usage: board_diagnostics_test <independent-probe> <artifact-prefix>");
        BoardDiagnosticsTest::VerifyInfoAndCallSequence(argv[1], argv[2]);
        BoardDiagnosticsTest::VerifyDebugAndQuietAcquisition(argv[1], argv[2]);
        BoardDiagnosticsTest::VerifyFailureDumpAndRetainedFrame(
            argv[1], std::string(argv[2]) + "-unknown", UINT32_C(0x80000000));
        BoardDiagnosticsTest::VerifyFailureDumpAndRetainedFrame(
            argv[1], std::string(argv[2]) + "-generic", (std::numeric_limits<std::uint32_t>::max)());
        BoardDiagnosticsTest::VerifyOffHasNoRecords(argv[1], argv[2]);
        BoardDiagnosticsTest::VerifyBindingFailure(argv[2]);
        BoardDiagnosticsTest::VerifyNativeInitializationFailure(argv[1], argv[2]);
        BoardDiagnosticsTest::VerifyPureExtremes(argv[2]);
        std::cout << "PASS VN10 initialization/configuration diagnostics, full failure context and quiet acquisition\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
