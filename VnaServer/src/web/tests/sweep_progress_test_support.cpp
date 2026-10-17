/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "sweep_progress_test_support.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "vna_core/frame_layout.h"

namespace SweepProgressTest {
void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const VnaWeb::Json &Field(const VnaWeb::Json &value, const std::string &name)
{
    const auto field = value.Find(name);
    Require(field != nullptr, "Missing field: " + name);
    return *field;
}

VnaCore::MeasurementFrame Frame(double reflected)
{
    VnaCore::MeasurementFrame frame;
    frame.channelId = 1U;
    frame.runId = 3U;
    frame.configRevision = 7U;
    frame.frameSeq = 1U;
    frame.plan.frequencyHz = {1.0, 2.0, 3.0, 4.0};
    frame.plan.frequencyRaw = {1U, 2U, 3U, 4U};
    frame.plan.sourcePorts = {1U};
    frame.plan.receiverMask = 1U;
    VnaCore::PortFrame port;
    port.sourcePort = 1U;
    port.iq.assign(4U * VnaCore::IQ_VALUES_PER_POINT, 0.0);
    for (std::size_t index = 0U; index < 4U; ++index) {
        port.iq[index * VnaCore::IQ_VALUES_PER_POINT] = 1.0;
        port.iq[index * VnaCore::IQ_VALUES_PER_POINT + 2U] = reflected;
    }
    frame.ports.push_back(std::move(port));
    return frame;
}

void ReplaceProgress(VnaService::InstrumentSnapshot &snapshot, VnaCore::MeasurementFrame frame, std::size_t count)
{
    VnaCore::SweepProgress progress;
    progress.frame = std::make_shared<const VnaCore::MeasurementFrame>(std::move(frame));
    progress.completedPoints = count;
    snapshot.channels.front().core.sweepProgress = std::make_shared<const VnaCore::SweepProgress>(std::move(progress));
}

namespace {
VnaCalibration::CalibrationPtr Correction()
{
    VnaCalibration::CalibrationData correction;
    correction.frequencyHz = {1.0, 2.0, 3.0, 4.0};
    correction.onePortTerms.assign(4U, {{0.1, 0.0}, {0.0, 0.0}, {2.0, 0.0}});
    return std::make_shared<const VnaCalibration::CalibrationData>(std::move(correction));
}

VnaDisplay::TracePresentation Presentation()
{
    VnaDisplay::TracePresentation presentation;
    presentation.name = "Trc1";
    presentation.format = VnaDisplay::DisplayFormat::REAL;
    auto &marker = presentation.markers.Active();
    marker.enabled = true;
    marker.hasSampleIndex = true;
    marker.sampleIndex = 0U;
    marker.acquisitionRevision = 7U;
    return presentation;
}
}  // 匿名命名空间

VnaService::InstrumentSnapshot Snapshot()
{
    VnaService::InstrumentChannelSnapshot channel;
    channel.id = 1U;
    channel.core.channelId = 1U;
    channel.core.state = VnaCore::CoreState::RUNNING;
    channel.core.runId = 3U;
    channel.core.configRevision = 7U;
    channel.settings.points = 4U;
    channel.settings.revision = 5U;
    channel.settings.acquisitionRevision = 7U;
    VnaService::Measurement measurement;
    measurement.name = "Trc1";
    measurement.channelId = 1U;
    Require(measurement.SetParameter("S11"), "Fixture S11 definition must be supported");
    channel.settings.measurements.push_back(measurement);
    channel.calibration.revision = 9U;
    channel.calibration.enabled = true;
    channel.calibration.compatible = true;
    channel.calibration.effective = true;
    channel.correction = Correction();
    VnaService::InstrumentSnapshot snapshot;
    snapshot.settings = channel.settings;
    snapshot.channels.push_back(std::move(channel));
    snapshot.display.revision = 6U;
    snapshot.display.traces.push_back(Presentation());
    auto partial = Frame(0.6);
    std::fill(partial.ports.front().iq.begin() + 2U * VnaCore::IQ_VALUES_PER_POINT, partial.ports.front().iq.end(),
        std::numeric_limits<double>::quiet_NaN());
    ReplaceProgress(snapshot, std::move(partial), 2U);
    return snapshot;
}

VnaWeb::Json Response(const VnaService::InstrumentSnapshot &snapshot, bool packed)
{
    const auto response =
        VnaWeb::ChannelFrameResponse(snapshot, snapshot.channels.front(), snapshot.settings.measurements, packed);
    Require(response.status == 200U, "Snapshot response must succeed");
    return response.json;
}
}  // 命名空间 SweepProgressTest
