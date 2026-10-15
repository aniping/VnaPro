/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "device/board/board_library.h"
#include "vna_core/acquisition_core.h"
#include "vna_core/frame_layout.h"

namespace {

constexpr std::size_t TEST_POINTS = 201U;
constexpr std::chrono::seconds g_ticketTimeout(3);

void Require(bool value, const char *message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaCore::CommandResult Await(const VnaCore::Ticket &ticket)
{
    Require(ticket.wait_for(g_ticketTimeout) == std::future_status::ready, "VN10 ticket timed out");
    const auto result = ticket.get();
    if (!result.ok) {
        throw std::runtime_error(result.message);
    }
    return result;
}

void SetMode(const char *mode)
{
#if defined(_WIN32)
    Require(_putenv_s("VNAPRO_SIM_MODE", mode) == 0 && _putenv_s("VNAPRO_SIM_SEED", "42") == 0,
        "could not set simulator environment");
#else
    Require(setenv("VNAPRO_SIM_MODE", mode, 1) == 0 && setenv("VNAPRO_SIM_SEED", "42", 1) == 0,
        "could not set simulator environment");
#endif
}

VnaCore::SweepPlan MakePlan(std::vector<std::uint32_t> sources)
{
    VnaCore::SweepPlan plan;
    for (std::size_t point = 0U; point < TEST_POINTS; ++point) {
        const auto frequency = UINT64_C(10000000) + UINT64_C(7990000000) * point / (TEST_POINTS - 1U);
        plan.frequencyRaw.push_back(frequency);
        plan.frequencyHz.push_back(static_cast<double>(frequency));
    }
    plan.powerDbm.assign(TEST_POINTS, 17.0);
    plan.ifBandwidthRaw = 1000U;
    plan.receiverMask = VnaCore::ALL_RECEIVERS_MASK;
    plan.sourcePorts = std::move(sources);
    plan.tableData.assign(TEST_POINTS * VnaCore::TABLE_VALUES_PER_POINT, std::numeric_limits<double>::quiet_NaN());
    return plan;
}

void DownloadReference(const VnaCore::Detail::BoardFunctions &functions, const VnaCore::SweepPlan &plan)
{
    auto frequencies = plan.frequencyRaw;
    auto powers = plan.powerDbm.empty() ? std::vector<double>(TEST_POINTS, 0.0) : plan.powerDbm;
    std::vector<std::uint32_t> bandwidths(TEST_POINTS, plan.ifBandwidthRaw);
    auto ports = plan.sourcePorts;
    const auto count = static_cast<std::uint32_t>(TEST_POINTS);
    functions.setFreqList(frequencies.data(), count);
    functions.setPowerList(powers.data(), count);
    functions.setIfBwList(bandwidths.data(), count);
    functions.setWorkPorts(ports.data(), static_cast<std::uint32_t>(ports.size()));
    Require(functions.dataFrameDownload() == 0U, "reference download failed");
}

std::vector<double> ReadReference(const std::string &path, const VnaCore::SweepPlan &plan)
{
    VnaCore::Detail::BoardLibrary library;
    library.Open(path);
    const auto &functions = library.Functions();
    try {
        Require(functions.hwInit() == 0U, "reference initialization failed");
        DownloadReference(functions, plan);
        Require(functions.setRfPowerOn(1U) == 0U && functions.startSweep() == 0U, "reference start failed");
        std::vector<double> result(TEST_POINTS * plan.sourcePorts.size() * VnaCore::IQ_VALUES_PER_POINT);
        // 直接 ABI 一次读取整帧，独立验证核心的 64 点分段及源端口解包过程。
        Require(functions.getSweepData(
                    0U, static_cast<std::uint32_t>(TEST_POINTS), result.data(), static_cast<int>(result.size())) == 0U,
            "reference complete read failed");
        Require(functions.setRfPowerOn(0U) == 0U, "reference RF-off failed");
        library.Close();
        return result;
    } catch (...) {
        functions.setRfPowerOn(0U);
        library.Close();
        throw;
    }
}

void VerifyFrame(
    const VnaCore::MeasurementFrame &frame, const VnaCore::SweepPlan &plan, const std::vector<double> &reference)
{
    Require(frame.ports.size() == plan.sourcePorts.size() && frame.plan.sourcePorts == plan.sourcePorts &&
                frame.plan.frequencyRaw == plan.frequencyRaw && frame.plan.powerDbm == plan.powerDbm,
        "VN10 frame lost the complete frozen plan");
    for (std::size_t source = 0U; source < frame.ports.size(); ++source) {
        const auto &port = frame.ports[source];
        Require(
            port.sourcePort == plan.sourcePorts[source] && port.iq.size() == TEST_POINTS * VnaCore::IQ_VALUES_PER_POINT,
            "VN10 source order or IQ shape mismatch");
        for (std::size_t point = 0U; point < TEST_POINTS; ++point) {
            for (std::size_t component = 0U; component < VnaCore::IQ_VALUES_PER_POINT; ++component) {
                const auto actual = port.iq[point * VnaCore::IQ_VALUES_PER_POINT + component];
                const auto expected =
                    reference[(source * TEST_POINTS + point) * VnaCore::IQ_VALUES_PER_POINT + component];
                Require(std::isfinite(actual) && std::fabs(actual - expected) <= 1e-12,
                    "VN10 segment boundary, source or component mismatch");
            }
        }
    }
}

void VerifyRecovery(VnaCore::AcquisitionCore &core, const VnaCore::SweepPlan &plan, const VnaCore::FramePtr &retained)
{
    auto invalid = plan;
    invalid.ifBandwidthRaw = 0U;
    const auto rejected = core.Configure(invalid).get();
    Require(!rejected.ok && rejected.message.find("DrvDataFrameDownload") != std::string::npos &&
                core.ReadView().state == VnaCore::CoreState::FAULTED && core.ReadView().frame == retained,
        "VN10 failed download replaced the published frame");
    const auto recovered = Await(core.Recover());
    Require(
        core.ReadView().state == VnaCore::CoreState::READY && recovered.configRevision == retained->configRevision + 1U,
        "VN10 recovery did not restore and version the previous complete plan");
    const auto restarted = Await(core.Start(VnaCore::RunMode::MANUAL));
    Await(core.TriggerOnce(restarted.runId, restarted.configRevision));
    Require(core.ReadView().frame->plan.ifBandwidthRaw == plan.ifBandwidthRaw, "VN10 recovery kept the failed plan");
    Await(core.Stop(restarted.runId));
}

void VerifyContinuousAndHold(VnaCore::AcquisitionCore &core)
{
    const auto started = Await(core.Start(VnaCore::RunMode::CONTINUOUS));
    const auto deadline = std::chrono::steady_clock::now() + g_ticketTimeout;
    for (;;) {
        const auto view = core.ReadView();
        if (view.frame && view.frame->runId == started.runId && view.frame->frameSeq >= 2U) {
            break;
        }
        Require(view.state == VnaCore::CoreState::RUNNING && std::chrono::steady_clock::now() < deadline,
            "VN10 continuous scanning stopped advancing");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto stopped = Await(core.Stop(started.runId));
    const auto held = core.ReadView().frame;
    Require(held && held->frameSeq == stopped.frameSeq && core.ReadView().state == VnaCore::CoreState::READY,
        "VN10 hold completed before the complete-frame boundary");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Require(core.ReadView().frame == held, "VN10 held frame continued changing");
}

void RunProfile(const std::string &path, std::vector<std::uint32_t> sources)
{
    auto plan = MakePlan(std::move(sources));
    if (plan.sourcePorts.size() == 2U) {
        plan.tableData.clear();  // 新 VN10 计划不必携带旧接口的 9N 表。
        plan.powerDbm.clear();   // 覆盖未提供逐点功率时的默认 0 dBm。
    }
    const auto reference = ReadReference(path, plan);
    VnaCore::AcquisitionCore core(path, VnaCore::DriverApi::TDC1VN10);
    Await(core.Initialize());
    Require(core.ReadView().deviceIdn == "VnaPro,TDC1VN10,4PORT,1.0", "VN10 adapter identity missing");
    const auto configured = Await(core.Configure(plan));
    auto shortPower = plan;
    shortPower.powerDbm.assign(TEST_POINTS, 0.0);
    shortPower.powerDbm.pop_back();
    Require(!core.Configure(shortPower).get().ok && core.ReadView().configRevision == configured.configRevision,
        "VN10 mismatched power length was accepted");
    const auto started = Await(core.Start(VnaCore::RunMode::MANUAL));
    Await(core.TriggerOnce(started.runId, started.configRevision));
    const auto retained = core.ReadView().frame;
    VerifyFrame(*retained, plan, reference);
    Await(core.TriggerOnce(started.runId, started.configRevision));
    Require(retained->frameSeq == 1U && core.ReadView().frame->frameSeq == 2U && core.ReadView().frame != retained,
        "VN10 retrigger mutated an immutable frame");
    VerifyFrame(*core.ReadView().frame, plan, reference);
    Await(core.Stop(started.runId));
    VerifyRecovery(core, plan, core.ReadView().frame);
    VerifyContinuousAndHold(core);
    Require(core.Close().ok, "VN10 shutdown failed");
    VerifyFrame(*retained, plan, reference);
}

void VerifySelectedAbi(const std::string &boardPath, const std::string &nativePath)
{
    VnaCore::AcquisitionCore wrongBoard(nativePath, VnaCore::DriverApi::TDC1VN10);
    const auto first = wrongBoard.Initialize().get();
    Require(!first.ok && first.message.find("DrvSetFreqList") != std::string::npos && wrongBoard.Close().ok,
        "VN08 library was called with VN10 signatures");
    VnaCore::AcquisitionCore wrongNative(boardPath, VnaCore::DriverApi::TDC1VN08);
    const auto second = wrongNative.Initialize().get();
    Require(!second.ok && second.message.find("DrvActivateRxLink") != std::string::npos && wrongNative.Close().ok,
        "VN10 library was called with VN08 signatures");
    VnaCore::AcquisitionCore relative("relative-driver-library", VnaCore::DriverApi::TDC1VN10);
    Require(!relative.Initialize().get().ok && relative.Close().ok, "relative VN10 library path was accepted");
}

void VerifyInitializationRecovery(const std::string &path)
{
    SetMode("noise ");
    VnaCore::AcquisitionCore core(path, VnaCore::DriverApi::TDC1VN10);
    Require(!core.Initialize().get().ok && core.ReadView().state == VnaCore::CoreState::FAULTED,
        "VN10 invalid environment was ignored");
    SetMode("golden");
    Await(core.Recover());
    Require(core.ReadView().state == VnaCore::CoreState::READY && core.Close().ok,
        "VN10 initialization failure could not be recovered and closed");
}

}  // 匿名命名空间

int main(int argc, char **argv)
{
    try {
        Require(argc == 3, "usage: board test <VN10 library> <VN08 library>");
        SetMode("golden");
        RunProfile(argv[1], {4U, 2U, 1U, 3U});
        RunProfile(argv[1], {2U, 1U});
        VerifySelectedAbi(argv[1], argv[2]);
        VerifyInitializationRecovery(argv[1]);
        std::cout << "core VN10 integration passed: two/four ports, segments, immutable frames, hold, recovery and ABI "
                     "selection\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
