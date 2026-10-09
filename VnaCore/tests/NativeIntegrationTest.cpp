#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "vna_core/AcquisitionCore.h"

namespace {

void require(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

vna_core::CommandResult await(const vna_core::Ticket& ticket)
{
    require(ticket.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "native ticket timed out");
    const auto result = ticket.get();
    if (!result.ok) {
        throw std::runtime_error(result.message);
    }
    return result;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "usage: native test <simulator library>");
        vna_core::AcquisitionCore core(argv[1]);
        await(core.initialize());
        require(core.readView().deviceIdn.find("VnaPro,NativeSimulator,") == 0U, "unexpected simulator identity");
        vna_core::SweepPlan plan;
        plan.frequencyRaw = {UINT64_C(1), UINT64_C(0x100000001)};
        plan.frequencyHz = {1.0, 4294967297.0};
        plan.ifBandwidthRaw = 1000U;
        plan.receiverMask = 0xFU;
        plan.sourcePorts = {1U, 2U, 3U, 4U};
        plan.tableData.assign(18U, std::numeric_limits<double>::quiet_NaN());
        const auto configured = await(core.configure(plan));
        const auto started = await(core.start(vna_core::RunMode::Manual));
        require(started.configRevision == configured.configRevision, "start changed the configuration revision");
        const auto triggered = await(core.triggerOnce(started.runId, configured.configRevision));
        const auto first = core.readView().frame;
        require(first && first->ports.size() == 4U && first->frameSeq == triggered.frameSeq,
            "multi-source frame was incomplete or trigger completed before publication");
        for (std::size_t source = 0U; source < 4U; ++source) {
            const auto& port = first->ports[source];
            require(port.sourcePort == source + 1U && port.iq.size() == 32U, "port conversion or IQ shape failed");
            require(std::fabs(port.iq[0] - (1.1 + 0.01 * static_cast<double>(source + 1U))) < 1e-12,
                "native source port numbering is incorrect");
            require(port.iq[1] != port.iq[17], "frequency high word was discarded");
            for (const auto value : port.iq) {
                require(std::isfinite(value), "simulator IQ contains invalid values");
            }
        }
        await(core.triggerOnce(started.runId, configured.configRevision));
        const auto second = core.readView().frame;
        require(
            second != first && second->frameSeq == 2U && first->frameSeq == 1U, "published frame was reused mutably");
        require(second->ports[3].iq == first->ports[3].iq, "same frozen configuration was not deterministic");
        await(core.stop(started.runId));
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
