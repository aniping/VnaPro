/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "test_support.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace HardwareBoardTest {
namespace {

void VerifyTiming(const VnaCore::SweepPlan &actual, const VnaCore::SweepPlan &expected)
{
    Require(actual.sweepTimeAuto == expected.sweepTimeAuto && actual.sweepTimeSeconds == expected.sweepTimeSeconds,
        "published frame lost its software sweep timing configuration");
}

}  // 匿名命名空间

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaCore::CommandResult Await(const VnaCore::Ticket &ticket)
{
    Require(ticket.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "hardware ticket timed out");
    return ticket.get();
}

VnaCore::CommandResult Success(const VnaCore::Ticket &ticket)
{
    const auto result = Await(ticket);
    Require(result.ok, result.message);
    return result;
}

VnaCore::SweepPlan Plan(std::vector<std::uint32_t> sources, std::size_t points)
{
    VnaCore::SweepPlan plan;
    for (std::size_t point = 0U; point < points; ++point) {
        const auto frequency = UINT64_C(10000000) + UINT64_C(1000000) * point;
        plan.frequencyRaw.push_back(frequency);
        plan.frequencyHz.push_back(static_cast<double>(frequency));
        plan.powerDbm.push_back(17.0 + static_cast<double>(point % 7U));
    }
    plan.ifBandwidthRaw = 1000U;
    plan.receiverMask = 0xFU;
    plan.sourcePorts = std::move(sources);
    return plan;
}

void VerifyFrame(const VnaCore::FramePtr &frame, const VnaCore::SweepPlan &plan, std::uint32_t sweep)
{
    Require(frame && frame->ports.size() == plan.sourcePorts.size(), "hardware frame lost a working source");
    Require(frame->plan.frequencyRaw == plan.frequencyRaw && frame->plan.powerDbm == plan.powerDbm &&
                frame->plan.sourcePorts == plan.sourcePorts && frame->plan.ifBandwidthRaw == plan.ifBandwidthRaw,
        "published hardware plan differs from downloaded plan");
    VerifyTiming(frame->plan, plan);
    for (std::size_t source = 0U; source < plan.sourcePorts.size(); ++source) {
        const auto &port = frame->ports[source];
        Require(port.sourcePort == plan.sourcePorts[source] && port.iq.size() == plan.frequencyRaw.size() * 16U,
            "hardware source order or IQ count differs");
        for (std::size_t point = 0U; point < plan.frequencyRaw.size(); ++point) {
            // 调用方独立求值：每接收端口 A实/A虚/B实/B虚，逐源连续全部请求频点。
            for (std::size_t receiver = 0U; receiver < 4U; ++receiver) {
                for (std::size_t component = 0U; component < 4U; ++component) {
                    const double expected = static_cast<double>(sweep) * 1000000.0 +
                                            static_cast<double>(plan.sourcePorts[source]) * 100000.0 +
                                            static_cast<double>(point) * 100.0 + static_cast<double>(receiver) * 4.0 +
                                            static_cast<double>(component);
                    Require(port.iq[point * 16U + receiver * 4U + component] == expected,
                        "hardware IQ component, source block or segment boundary differs");
                }
            }
        }
    }
}

}  // 命名空间 HardwareBoardTest
