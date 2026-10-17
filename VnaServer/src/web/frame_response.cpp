/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/frame_response.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include "vna_measurement/measurement_processor.h"
#include "vna_service/instrument_profile.h"
#include "web/display_json.h"
#include "web/frame_array_json.h"
#include "web/http_api.h"

namespace VnaWeb {
Json MeasurementJson(const VnaService::Measurement &measurement, const VnaCalibration::CalibrationPtr &calibration)
{
    Json value = Json::MakeObject();
    value["name"] = Json(measurement.name);
    value["channelId"] = Json(measurement.channelId);
    value["receiverKind"] = Json(std::string(1, measurement.receiverKind));
    value["receiverPort"] = Json(measurement.receiverPort);
    value["sourcePort"] = Json(measurement.sourcePort);
    value["parameter"] = Json(measurement.Parameter());
    const bool ratio = measurement.kind == VnaService::MeasurementKind::S_PARAMETER;
    value["kind"] = Json(ratio ? "sParameter" : "receiver");
    value["unit"] = Json(ratio ? "1" : "a.u.");
    const bool corrected =
        ratio && calibration && calibration->Covers(measurement.receiverPort, measurement.sourcePort);
    value["calibration"] = Json(!ratio ? "raw" : corrected ? "calibrated" : "uncalibrated");
    return value;
}

namespace {
// 即使请求128条迹线，也不同时保留全部完整频轴的复数结果。
constexpr std::size_t CALCULATION_BATCH_SIZE = 4U;

struct FrameEncoding final {
    bool packed = false;
    bool preview = false;
    std::size_t completedPoints = 0U;
};

struct FrameProjection final {
    const VnaService::InstrumentSnapshot &snapshot;
    const VnaService::InstrumentChannelSnapshot &channel;
    const VnaCore::MeasurementFrame &frame;
    std::vector<std::size_t> indexes;
    FrameEncoding encoding;
};

void AppendFrameAxis(
    Json &value, const VnaCore::MeasurementFrame &frame, const std::vector<std::size_t> &indexes, bool packed)
{
    value["frequencyHz"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&frame, &indexes](std::size_t position) {
            return FrameArrayNumber{frame.plan.frequencyHz[indexes[position]], true};
        },
        packed);
    value["sampleIndexes"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&indexes](std::size_t position) {
            return FrameArrayNumber{static_cast<double>(indexes[position]), true};
        },
        packed);
}

Json TraceJson(const FrameProjection &context, const VnaService::Measurement &measurement,
    const VnaDisplay::TracePresentation &presentation, const VnaMeasurement::TraceResult &result)
{
    const auto &indexes = context.indexes;
    const bool packed = context.encoding.packed;
    Json trace = MeasurementJson(measurement);
    if (measurement.kind == VnaService::MeasurementKind::S_PARAMETER) {
        trace["calibration"] = Json(result.calibrated ? "calibrated" : "uncalibrated");
    }
    const auto validAt = [&result, &indexes](std::size_t position) {
        const auto index = indexes[position];
        return result.available && index < result.points.size() && result.points[index].valid;
    };
    trace["available"] = Json(result.available);
    trace["real"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&result, &indexes, &validAt](std::size_t position) {
            const bool valid = validAt(position);
            return FrameArrayNumber{valid ? result.points[indexes[position]].real : 0.0, valid};
        },
        packed);
    trace["imag"] = FrameArrayJson::Numbers(
        indexes.size(),
        [&result, &indexes, &validAt](std::size_t position) {
            const bool valid = validAt(position);
            return FrameArrayNumber{valid ? result.points[indexes[position]].imag : 0.0, valid};
        },
        packed);
    trace["valid"] = FrameArrayJson::Booleans(indexes.size(), validAt, packed);
    auto projection = VnaDisplay::TraceProjection::Calculate(
        presentation, result, context.frame.plan.frequencyHz, measurement, context.frame.configRevision);
    if (context.encoding.preview) {
        projection.marker = {};
        projection.markers.assign(VnaDisplay::MAX_MARKERS, VnaDisplay::MarkerReadout{});
    }
    trace["projection"] = DisplayJson::Projection(projection, presentation.format, indexes, packed);
    return trace;
}

Json FrameTraces(const FrameProjection &context, const std::vector<VnaService::Measurement> &selected)
{
    Json traces = Json::MakeArray();
    for (std::size_t begin = 0U; begin < selected.size(); begin += CALCULATION_BATCH_SIZE) {
        const auto end = (std::min)(selected.size(), begin + CALCULATION_BATCH_SIZE);
        const std::vector<VnaService::Measurement> batch(selected.begin() + begin, selected.begin() + end);
        auto results =
            VnaMeasurement::MeasurementProcessor::CalculateMany(context.frame, batch, context.channel.correction.get());
        if (context.encoding.preview) {
            for (auto &result : results) {
                for (auto index = context.encoding.completedPoints; index < result.points.size(); ++index) {
                    result.points[index] = {};
                }
            }
        }
        for (std::size_t offset = 0U; offset < batch.size(); ++offset) {
            const auto &measurement = batch[offset];
            const auto &presentations = context.snapshot.display.traces;
            const auto presentation = std::find_if(
                presentations.begin(), presentations.end(), [&measurement](const VnaDisplay::TracePresentation &item) {
                    return item.name == measurement.name;
                });
            if (presentation != presentations.end()) {
                traces.array.push_back(TraceJson(context, measurement, *presentation, results[offset]));
            }
        }
    }
    return traces;
}

bool CurrentFrame(const VnaService::InstrumentChannelSnapshot &channel, const VnaCore::MeasurementFrame &frame)
{
    return !channel.finitePending && channel.core.state != VnaCore::CoreState::FAULTED &&
           channel.core.state != VnaCore::CoreState::CLOSED && frame.channelId == channel.id &&
           frame.configRevision == channel.settings.acquisitionRevision &&
           frame.configRevision == channel.core.configRevision && frame.runId == channel.core.runId;
}

Json FrameMetadata(const FrameProjection &context)
{
    const auto &frame = context.frame;
    Json value = Json::MakeObject();
    value["available"] = Json(true);
    value["current"] = Json(!context.encoding.preview && CurrentFrame(context.channel, frame));
    value["channelId"] = Json(frame.channelId);
    value["displayRevision"] = Json(std::to_string(context.snapshot.display.revision));
    value["calibrationRevision"] = Json(std::to_string(context.channel.calibration.revision));
    value["runId"] = Json(std::to_string(frame.runId));
    value["revision"] = Json(std::to_string(frame.configRevision));
    value["measurementRevision"] = Json(std::to_string(context.snapshot.settings.revision));
    value["frameSeq"] = Json(std::to_string(frame.frameSeq));
    value["originalPointCount"] = Json(static_cast<unsigned>(context.indexes.size()));
    value["displayPointCount"] = Json(static_cast<unsigned>(context.indexes.size()));
    value["decimated"] = Json(false);
    return value;
}

HttpResponse EncodeFrame(const VnaService::InstrumentSnapshot &snapshot,
    const VnaService::InstrumentChannelSnapshot &channel, const VnaCore::MeasurementFrame &frame,
    const std::vector<VnaService::Measurement> &selected, FrameEncoding encoding)
{
    const auto count = frame.plan.frequencyHz.size();
    if (count == 0U) {
        return HttpError(409, "no_physical_axis", "Frame does not have a verified physical frequency axis");
    }
    if (count > VnaService::MAX_SWEEP_POINTS || selected.size() > VnaDisplay::MAX_DISPLAY_TRACES) {
        return HttpError(409, "frame_capacity_exceeded", "Frame exceeds the supported full-sweep display capacity");
    }
    FrameProjection context{snapshot, channel, frame, {}, encoding};
    context.indexes.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        context.indexes.push_back(index);
    }
    HttpResponse response;
    response.json = FrameMetadata(context);
    AppendFrameAxis(response.json, frame, context.indexes, encoding.packed);
    response.json["traces"] = FrameTraces(context, selected);
    return response;
}

bool HealthyProgress(const VnaService::InstrumentChannelSnapshot &channel)
{
    return channel.core.state == VnaCore::CoreState::RUNNING && channel.core.error.empty() &&
           channel.core.sweepProgress && channel.core.sweepProgress->frame && !channel.calibration.collecting &&
           !channel.calibration.capturePending;
}

bool ProgressIdentity(const VnaService::InstrumentChannelSnapshot &channel, const VnaCore::MeasurementFrame &frame)
{
    if (frame.channelId != channel.id || channel.core.channelId != channel.id || frame.runId == 0U) {
        return false;
    }
    return frame.runId == channel.core.runId && frame.configRevision != 0U &&
           frame.configRevision == channel.settings.acquisitionRevision &&
           frame.configRevision == channel.core.configRevision;
}

bool ProgressPrefix(const VnaService::InstrumentChannelSnapshot &channel, const VnaCore::SweepProgress &progress)
{
    const auto &frame = *progress.frame;
    const auto count = frame.plan.frequencyHz.size();
    if (progress.completedPoints == 0U || progress.completedPoints >= count ||
        frame.plan.frequencyRaw.size() != count || count != channel.settings.points) {
        return false;
    }
    return !channel.correction || channel.correction->frequencyHz == frame.plan.frequencyHz;
}

bool NextProgressFrame(const VnaService::InstrumentChannelSnapshot &channel, const VnaCore::MeasurementFrame &frame)
{
    const auto &complete = channel.core.frame;
    const auto sequence = complete && complete->runId == frame.runId ? complete->frameSeq : 0U;
    return sequence != std::numeric_limits<std::uint64_t>::max() && frame.frameSeq == sequence + 1U;
}

bool CurrentProgress(const VnaService::InstrumentChannelSnapshot &channel)
{
    if (!HealthyProgress(channel)) {
        return false;
    }
    const auto &progress = *channel.core.sweepProgress;
    return ProgressIdentity(channel, *progress.frame) && ProgressPrefix(channel, progress) &&
           NextProgressFrame(channel, *progress.frame);
}
}  // 匿名命名空间

HttpResponse ChannelFrameResponse(const VnaService::InstrumentSnapshot &snapshot,
    const VnaService::InstrumentChannelSnapshot &channel, const std::vector<VnaService::Measurement> &selection,
    bool packed)
{
    std::vector<VnaService::Measurement> selected;
    for (const auto &measurement : selection) {
        if (measurement.channelId == channel.id) {
            selected.push_back(measurement);
        }
    }
    HttpResponse response;
    if (channel.core.frame) {
        response = EncodeFrame(snapshot, channel, *channel.core.frame, selected, {packed, false, 0U});
    } else {
        response.json = Json::MakeObject();
        response.json["channelId"] = Json(channel.id);
        response.json["displayRevision"] = Json(std::to_string(snapshot.display.revision));
        response.json["calibrationRevision"] = Json(std::to_string(channel.calibration.revision));
        response.json["available"] = Json(false);
        response.json["current"] = Json(false);
        response.json["reason"] = Json("No completed frame");
    }
    if (response.status == 200U && CurrentProgress(channel)) {
        const auto &progress = *channel.core.sweepProgress;
        auto encoded =
            EncodeFrame(snapshot, channel, *progress.frame, selected, {packed, true, progress.completedPoints});
        if (encoded.status == 200U) {
            auto value = Json::MakeObject();
            value["completedPoints"] = Json(static_cast<unsigned>(progress.completedPoints));
            value["frame"] = std::move(encoded.json);
            response.json["sweepProgress"] = std::move(value);
        }
    }
    return response;
}
}  // 命名空间 VnaWeb
