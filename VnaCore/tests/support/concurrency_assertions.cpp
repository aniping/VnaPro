/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "concurrency_assertions.h"

namespace vna_core_test {

void Require(bool value, const char *message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

CommandResult Await(const Ticket &ticket)
{
    Require(ticket.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "ticket did not terminate");
    return ticket.get();
}

SweepPlan MakePlan(std::uint32_t variant)
{
    SweepPlan plan;
    plan.frequencyRaw = {UINT64_C(1000000) + variant, UINT64_C(0x100000001) + variant};
    if (variant % 2U != 0U) {
        plan.frequencyRaw.push_back(UINT64_C(0x200000003) + variant);
    }
    for (const auto frequency : plan.frequencyRaw) {
        plan.frequencyHz.push_back(static_cast<double>(frequency));
    }
    plan.ifBandwidthRaw = 1000U + variant;
    plan.receiverMask = variant % 2U == 0U ? 0xFU : 0x5U;
    plan.sourcePorts = variant % 2U == 0U ? std::vector<std::uint32_t>{1U, 4U} : std::vector<std::uint32_t>{4U, 2U, 1U};
    plan.tableData.assign(9U * plan.frequencyRaw.size(), 0.5 + static_cast<double>(variant) / 1024.0);
    return plan;
}

bool SamePlan(const SweepPlan &left, const SweepPlan &right)
{
    return left.frequencyRaw == right.frequencyRaw && left.frequencyHz == right.frequencyHz &&
           left.ifBandwidthRaw == right.ifBandwidthRaw && left.receiverMask == right.receiverMask &&
           left.sourcePorts == right.sourcePorts && left.tableData == right.tableData;
}

double SampleValue(
    const SweepPlan &plan, std::uint64_t frame, std::uint32_t source, std::size_t point, std::size_t slot)
{
    // Exact binary fields distinguish configuration, frame, source, point and all 16 IQ slots.
    return static_cast<double>(frame) * 68719476736.0 + static_cast<double>(plan.ifBandwidthRaw) * 1048576.0 +
           static_cast<double>(source) * 65536.0 + static_cast<double>(point) * 256.0 +
           static_cast<double>(slot / 4U) * 16.0 + static_cast<double>(slot % 4U) +
           static_cast<double>(plan.frequencyRaw[point] & UINT64_C(0x3FF)) / 1024.0;
}

void RequireFrameContent(const MeasurementFrame &frame)
{
    Require(frame.ports.size() == frame.plan.sourcePorts.size(), "published frame is missing a source");
    for (std::size_t source = 0U; source < frame.ports.size(); ++source) {
        const auto &port = frame.ports[source];
        Require(port.sourcePort == frame.plan.sourcePorts[source], "published sources were mixed or reordered");
        Require(port.iq.size() == 16U * frame.plan.frequencyRaw.size(), "published IQ shape is incomplete");
        for (std::size_t point = 0U; point < frame.plan.frequencyRaw.size(); ++point) {
            for (std::size_t receiver = 0U; receiver < 4U; ++receiver) {
                for (std::size_t component = 0U; component < 4U; ++component) {
                    Require(port.iq[16U * point + 4U * receiver + component] == SampleValue(frame.plan, frame.frameSeq,
                                                                                    port.sourcePort, point,
                                                                                    (receiver * 4U) + component),
                        "published IQ mixes configurations, frames, points or receivers");
                }
            }
        }
    }
}

void RequireUnchanged(const MeasurementFrame &frame, const MeasurementFrame &snapshot)
{
    Require(frame.channelId == snapshot.channelId && frame.runId == snapshot.runId &&
                frame.configRevision == snapshot.configRevision && frame.frameSeq == snapshot.frameSeq &&
                SamePlan(frame.plan, snapshot.plan) && frame.ports.size() == snapshot.ports.size(),
        "retained frame metadata or full plan was mutated");
    for (std::size_t source = 0U; source < frame.ports.size(); ++source) {
        Require(frame.ports[source].sourcePort == snapshot.ports[source].sourcePort &&
                    frame.ports[source].iq == snapshot.ports[source].iq,
            "retained frame source or IQ buffer was mutated");
    }
}

}  // namespace vna_core_test
