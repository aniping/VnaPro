/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_TEST_SUPPORT_H
#define VNAPRO_VNA_DISPLAY_TEST_SUPPORT_H

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vna_core/frame_layout.h"
#include "vna_display/display_workspace.h"
#include "vna_measurement/trace_result.h"

namespace DisplayTest {

inline void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline void Near(double actual, double expected)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= 1e-12 * std::max(1.0, std::abs(expected)),
        "显示值与独立数值期望不一致");
}

inline VnaMeasurement::Measurement Measurement(const std::string &name = "Trc1", unsigned receiver = 1U)
{
    VnaMeasurement::Measurement value;
    value.name = name;
    value.kind = VnaMeasurement::MeasurementKind::S_PARAMETER;
    value.receiverKind = 'b';
    value.receiverPort = receiver;
    return value;
}

inline VnaMeasurement::TraceResult Trace(const std::vector<double> &real, const std::vector<double> &imag = {})
{
    VnaMeasurement::TraceResult trace;
    trace.available = true;
    for (std::size_t index = 0U; index < real.size(); ++index) {
        VnaMeasurement::ComplexPoint point;
        point.real = real[index];
        point.imag = imag.empty() ? 0.0 : imag[index];
        point.valid = true;
        trace.points.push_back(point);
    }
    return trace;
}

inline VnaDisplay::FrameContext Frame(std::size_t count = 3U, bool cw = false)
{
    auto frame = std::make_shared<VnaCore::MeasurementFrame>();
    frame->configRevision = 7U;
    frame->runId = 3U;
    frame->frameSeq = 11U;
    frame->plan.sourcePorts = {1U};
    frame->plan.receiverMask = VnaCore::ALL_RECEIVERS_MASK;
    VnaCore::PortFrame port;
    port.sourcePort = 1U;
    port.iq.resize(count * VnaCore::IQ_VALUES_PER_POINT, 0.0);
    for (std::size_t index = 0U; index < count; ++index) {
        frame->plan.frequencyRaw.push_back(index + 1U);
        frame->plan.frequencyHz.push_back(cw ? 10e6 : static_cast<double>(index + 1U) * 1e6);
        const auto offset = index * VnaCore::IQ_VALUES_PER_POINT;
        port.iq[offset] = 1.0;
        port.iq[offset + 2U] = 0.1 + static_cast<double>(index) * 0.01;
    }
    frame->ports.push_back(std::move(port));
    VnaDisplay::FrameContext context;
    context.frame = frame;
    context.acquisitionRevision = 7U;
    context.runId = 3U;
    context.current = true;
    return context;
}

inline VnaDisplay::DisplayChange Change(VnaDisplay::DisplayChangeKind kind, const std::string &name = "Trc1")
{
    VnaDisplay::DisplayChange change;
    change.kind = kind;
    change.name = name;
    return change;
}

}  // 命名空间 DisplayTest

#endif
