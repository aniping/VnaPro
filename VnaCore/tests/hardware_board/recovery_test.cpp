/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>

#include "test_environment.h"
#include "test_support.h"

namespace HardwareBoardTest {

void VerifyIdleRecovery(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace, "DOWNLOAD", 2U);
    const auto plan = Plan({2U, 1U, 4U, 3U});
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    const auto configured = Success(core.Configure(plan));
    const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
    Success(core.TriggerOnce(started.runId, started.configRevision));
    Success(core.Stop(started.runId));
    const auto retained = core.ReadView().frame;
    VerifyFrame(retained, plan, 1U);
    const auto failed = Await(core.Configure(plan));
    Require(!failed.ok && failed.message.find("DrvDataFrameDownload returned 731") != std::string::npos &&
                core.ReadView().state == VnaCore::CoreState::FAULTED && core.ReadView().frame == retained,
        "idle hardware download failure did not preserve the last complete frame");
    const auto recovered = Success(core.Recover());
    Require(core.ReadView().state == VnaCore::CoreState::READY && core.ReadView().frame == retained &&
                recovered.configRevision == configured.configRevision + 1U,
        "idle hardware recovery lost the complete plan or failed to advance its revision");
    const auto events = environment.Events();
    const auto init = std::find(events.rbegin(), events.rend(), "INIT");
    Require(init != events.rend() && std::count(events.begin(), events.end(), "INIT") == 2 &&
                std::count(events.begin(), events.end(), "DOWNLOAD") == 3,
        "idle hardware recovery did not reinitialize and restore the previous downloaded plan");
    const std::vector<std::string> expectedTail = {
        "INIT", "RF_OFF", "FREQ 130", "POWER 130", "IFBW 130", "PORTS 4", "DOWNLOAD"};
    Require(events.size() >= expectedTail.size() &&
                std::equal(expectedTail.begin(), expectedTail.end(), events.end() - expectedTail.size()) &&
                std::find(events.begin(), events.end(), "UNLOAD") == events.end(),
        "idle recovery unloaded the driver or started scanning without a command");
    const auto restarted = Success(core.Start(VnaCore::RunMode::MANUAL));
    Success(core.TriggerOnce(restarted.runId, restarted.configRevision));
    VerifyFrame(core.ReadView().frame, plan, 1U);
    Require(core.ReadView().frame != retained && core.ReadView().frame->runId != retained->runId,
        "recovered hardware acquisition reused the prior run frame");
    Require(core.Close().ok, "idle-recovered hardware shutdown failed");
    VerifyFrame(retained, plan, 1U);
    const auto completed = environment.Events();
    Require(completed.back() == "RF_OFF" && std::find(completed.begin(), completed.end(), "UNLOAD") == completed.end(),
        "idle-recovered hardware driver was incorrectly unloaded");
    environment.VerifyThreadAndEnvironment();
}

}  // 命名空间 HardwareBoardTest
