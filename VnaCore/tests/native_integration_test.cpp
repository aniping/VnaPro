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
#include <vector>

#include "../../VnaNativeSimulator/tests/golden_iq_vectors.h"
#include "vna_core/acquisition_core.h"

namespace {

void Require(bool value, const char *message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

vna_core::CommandResult Await(const vna_core::Ticket &ticket)
{
    Require(ticket.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "native ticket timed out");
    const auto result = ticket.get();
    if (!result.ok) {
        throw std::runtime_error(result.message);
    }
    return result;
}

void VerifyFrame(const vna_core::MeasurementFrame *frame, const vna_core::SweepPlan &plan,
    const std::vector<std::vector<double>> &expected, const vna_core::CommandResult &triggered)
{
    Require(frame && frame->ports.size() == expected.size(), "multi-source frame was incomplete");
    Require(frame->frameSeq == triggered.frameSeq && frame->runId == triggered.runId &&
                frame->configRevision == triggered.configRevision,
        "trigger completed before publishing its frame");
    Require(frame->plan.frequencyRaw == plan.frequencyRaw && frame->plan.frequencyHz == plan.frequencyHz &&
                frame->plan.sourcePorts == plan.sourcePorts && frame->plan.receiverMask == plan.receiverMask,
        "published frame did not retain the complete frozen sweep plan");
    for (std::size_t source = 0U; source < expected.size(); ++source) {
        const auto &port = frame->ports[source];
        Require(port.sourcePort == plan.sourcePorts[source] && port.iq.size() == expected[source].size(),
            "source port conversion or IQ shape failed");
        for (std::size_t index = 0U; index < port.iq.size(); ++index) {
            if (!std::isfinite(port.iq[index]) || std::fabs(port.iq[index] - expected[source][index]) > 1e-12) {
                throw std::runtime_error("golden IQ mismatch at source " + std::to_string(port.sourcePort) +
                                         ", point " + std::to_string(index / 16U) + ", component " +
                                         std::to_string(index % 16U));
            }
        }
    }
}

void SelectGoldenProfile()
{
#ifdef _WIN32
    Require(_putenv_s("VNAPRO_SIM_MODE", "golden") == 0 && _putenv_s("VNAPRO_SIM_SEED", "") == 0,
        "could not select the fixed simulator profile");
#else
    Require(setenv("VNAPRO_SIM_MODE", "golden", 1) == 0 && unsetenv("VNAPRO_SIM_SEED") == 0,
        "could not select the fixed simulator profile");
#endif
}

void VerifyHighWord(
    vna_core::AcquisitionCore &core, vna_core::SweepPlan plan, const vna_core::MeasurementFrame &retained)
{
    // The low words are equal; both complete RI vectors must match independent literals.
    plan.frequencyRaw[1] = vna_test::GOLDEN_IQ_HIGH_WORD_FREQUENCY;
    plan.frequencyHz[1] = static_cast<double>(vna_test::GOLDEN_IQ_HIGH_WORD_FREQUENCY);
    plan.sourcePorts = {1U};
    const double *low = vna_test::g_goldenIqBySource[0U][0U];
    std::vector<double> highWordExpected(low, low + 16U);
    highWordExpected.insert(
        highWordExpected.end(), vna_test::g_goldenIqHighWordSource0, vna_test::g_goldenIqHighWordSource0 + 16U);
    const auto highConfigured = Await(core.Configure(plan));
    const auto highStarted = Await(core.Start(vna_core::RunMode::MANUAL));
    const auto highTriggered = Await(core.TriggerOnce(highStarted.runId, highConfigured.configRevision));
    VerifyFrame(core.ReadView().frame.get(), plan, {highWordExpected}, highTriggered);
    Require(retained.ports.size() == 4U && retained.plan.frequencyRaw[1] == vna_test::g_goldenIqFrequencies[1],
        "reconfiguration modified an old published frame");
    Await(core.Stop(highStarted.runId));
    Require(core.Close().ok && core.ReadView().state == vna_core::CoreState::CLOSED, "native close failed");
}

void VerifyLoaderFailures(const std::string &libraryPath)
{
    vna_core::AcquisitionCore missing(libraryPath + ".does-not-exist");
    Require(!missing.Initialize().get().ok && missing.ReadView().state == vna_core::CoreState::FAULTED,
        "loader failure did not terminate initialization with Faulted");
    Require(missing.Close().ok, "failed initialization could not be closed");
    vna_core::AcquisitionCore relative("C:does-not-exist-vna-core-test-library");
    const auto relativeResult = relative.Initialize().get();
    Require(!relativeResult.ok && relativeResult.message.find("absolute") != std::string::npos,
        "relative or drive-relative native path was allowed");
    Require(relative.Close().ok, "rejected relative path could not be closed");
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        Require(argc == 2, "usage: native test <simulator library>");
        SelectGoldenProfile();
        vna_core::AcquisitionCore core(argv[1]);
        Await(core.Initialize());
        Require(core.ReadView().deviceIdn.find("VnaPro,NativeSimulator,") == 0U, "unexpected simulator identity");
        vna_core::SweepPlan plan;
        plan.frequencyRaw = {vna_test::g_goldenIqFrequencies[0], vna_test::g_goldenIqFrequencies[1]};
        plan.frequencyHz = {1000000000.0, 2000000000.0};
        plan.ifBandwidthRaw = 1000U;
        plan.receiverMask = 0xFU;
        plan.sourcePorts = {1U, 2U, 3U, 4U};
        plan.tableData.assign(18U, std::numeric_limits<double>::quiet_NaN());
        std::vector<std::vector<double>> expected;
        for (std::uint32_t source = 0U; source < 4U; ++source) {
            expected.push_back(vna_test::GoldenIqForSource(source));
        }
        const auto configured = Await(core.Configure(plan));
        const auto started = Await(core.Start(vna_core::RunMode::MANUAL));
        Require(started.configRevision == configured.configRevision, "start changed the configuration revision");
        const auto triggered = Await(core.TriggerOnce(started.runId, configured.configRevision));
        const auto first = core.ReadView().frame;
        VerifyFrame(first.get(), plan, expected, triggered);
        const auto retriggered = Await(core.TriggerOnce(started.runId, configured.configRevision));
        const auto second = core.ReadView().frame;
        VerifyFrame(second.get(), plan, expected, retriggered);
        Require(
            second != first && second->frameSeq == 2U && first->frameSeq == 1U, "published frame was reused mutably");
        for (std::size_t source = 0U; source < expected.size(); ++source) {
            Require(
                second->ports[source].iq == first->ports[source].iq, "same frozen configuration was not deterministic");
        }
        Await(core.Stop(started.runId));

        VerifyHighWord(core, plan, *second);
        VerifyLoaderFailures(argv[1]);
        std::cout << "core native integration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
