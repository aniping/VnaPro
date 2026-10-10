#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../VnaNativeSimulator/tests/GoldenIqVectors.h"
#include "vna_core/AcquisitionCore.h"

namespace {

void require(bool value, const char *message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

vna_core::CommandResult await(const vna_core::Ticket &ticket)
{
    require(ticket.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "native ticket timed out");
    const auto result = ticket.get();
    if (!result.ok) {
        throw std::runtime_error(result.message);
    }
    return result;
}

void verifyFrame(const vna_core::FramePtr &frame, const vna_core::SweepPlan &plan,
    const std::vector<std::vector<double>> &expected, const vna_core::CommandResult &triggered)
{
    require(frame && frame->ports.size() == expected.size(), "multi-source frame was incomplete");
    require(frame->frameSeq == triggered.frameSeq && frame->runId == triggered.runId &&
                frame->configRevision == triggered.configRevision,
        "trigger completed before publishing its frame");
    require(frame->plan.frequencyRaw == plan.frequencyRaw && frame->plan.frequencyHz == plan.frequencyHz &&
                frame->plan.sourcePorts == plan.sourcePorts && frame->plan.receiverMask == plan.receiverMask,
        "published frame did not retain the complete frozen sweep plan");
    for (std::size_t source = 0U; source < expected.size(); ++source) {
        const auto &port = frame->ports[source];
        require(port.sourcePort == plan.sourcePorts[source] && port.iq.size() == expected[source].size(),
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

}  // namespace

int main(int argc, char **argv)
{
    try {
        require(argc == 2, "usage: native test <simulator library>");
#ifdef _WIN32
        require(_putenv_s("VNAPRO_SIM_MODE", "golden") == 0 && _putenv_s("VNAPRO_SIM_SEED", "") == 0,
            "could not select the fixed simulator profile");
#else
        require(setenv("VNAPRO_SIM_MODE", "golden", 1) == 0 && unsetenv("VNAPRO_SIM_SEED") == 0,
            "could not select the fixed simulator profile");
#endif
        vna_core::AcquisitionCore core(argv[1]);
        await(core.initialize());
        require(core.readView().deviceIdn.find("VnaPro,NativeSimulator,") == 0U, "unexpected simulator identity");
        vna_core::SweepPlan plan;
        plan.frequencyRaw = {vna_test::kGoldenIqFrequencies[0], vna_test::kGoldenIqFrequencies[1]};
        plan.frequencyHz = {1000000000.0, 2000000000.0};
        plan.ifBandwidthRaw = 1000U;
        plan.receiverMask = 0xFU;
        plan.sourcePorts = {1U, 2U, 3U, 4U};
        plan.tableData.assign(18U, std::numeric_limits<double>::quiet_NaN());
        std::vector<std::vector<double>> expected;
        for (std::uint32_t source = 0U; source < 4U; ++source) {
            expected.push_back(vna_test::GoldenIqForSource(source));
        }
        const auto configured = await(core.configure(plan));
        const auto started = await(core.start(vna_core::RunMode::Manual));
        require(started.configRevision == configured.configRevision, "start changed the configuration revision");
        const auto triggered = await(core.triggerOnce(started.runId, configured.configRevision));
        const auto first = core.readView().frame;
        verifyFrame(first, plan, expected, triggered);
        const auto retriggered = await(core.triggerOnce(started.runId, configured.configRevision));
        const auto second = core.readView().frame;
        verifyFrame(second, plan, expected, retriggered);
        require(
            second != first && second->frameSeq == 2U && first->frameSeq == 1U, "published frame was reused mutably");
        for (std::size_t source = 0U; source < expected.size(); ++source) {
            require(
                second->ports[source].iq == first->ports[source].iq, "same frozen configuration was not deterministic");
        }
        await(core.stop(started.runId));

        // The low words are equal; both complete RI vectors must match independent literals.
        plan.frequencyRaw[1] = vna_test::kGoldenIqHighWordFrequency;
        plan.frequencyHz[1] = static_cast<double>(vna_test::kGoldenIqHighWordFrequency);
        plan.sourcePorts = {1U};
        const double *low = vna_test::kGoldenIqBySource[0U][0U];
        std::vector<double> highWordExpected(low, low + 16U);
        highWordExpected.insert(
            highWordExpected.end(), vna_test::kGoldenIqHighWordSource0, vna_test::kGoldenIqHighWordSource0 + 16U);
        const auto highConfigured = await(core.configure(plan));
        const auto highStarted = await(core.start(vna_core::RunMode::Manual));
        const auto highTriggered = await(core.triggerOnce(highStarted.runId, highConfigured.configRevision));
        verifyFrame(core.readView().frame, plan, {highWordExpected}, highTriggered);
        require(second->ports.size() == 4U && second->plan.frequencyRaw[1] == vna_test::kGoldenIqFrequencies[1],
            "reconfiguration modified an old published frame");
        await(core.stop(highStarted.runId));
        require(core.close().ok && core.readView().state == vna_core::CoreState::Closed, "native close failed");

        vna_core::AcquisitionCore missing(std::string(argv[1]) + ".does-not-exist");
        require(!missing.initialize().get().ok && missing.readView().state == vna_core::CoreState::Faulted,
            "loader failure did not terminate initialization with Faulted");
        require(missing.close().ok, "failed initialization could not be closed");
        vna_core::AcquisitionCore relative("C:does-not-exist-vna-core-test-library");
        const auto relativeResult = relative.initialize().get();
        require(!relativeResult.ok && relativeResult.message.find("absolute") != std::string::npos,
            "relative or drive-relative native path was allowed");
        require(relative.close().ok, "rejected relative path could not be closed");
        std::cout << "core native integration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
