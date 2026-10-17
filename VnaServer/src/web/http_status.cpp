/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <string>
#include <utility>

#include "vna_service/instrument_profile.h"
#include "web/calibration_json.h"
#include "web/display_json.h"
#include "web/frame_response.h"
#include "web/http_api.h"
#include "web/stimulus_zoom_json.h"

namespace VnaWeb {
namespace {
std::string CoreStateName(VnaCore::CoreState value)
{
    switch (value) {
        case VnaCore::CoreState::UNINITIALIZED:
            return "Uninitialized";
        case VnaCore::CoreState::READY:
            return "Ready";
        case VnaCore::CoreState::RUNNING:
            return "Running";
        case VnaCore::CoreState::FAULTED:
            return "Faulted";
        case VnaCore::CoreState::CLOSED:
            return "Closed";
        default:
            return "Uninitialized";
    }
}

Json Capabilities(const VnaService::InstrumentProfile &profile, const VnaCore::CoreView &core)
{
    Json capabilities = Json::MakeObject();
    capabilities["portCount"] = Json(profile.portCount);
    capabilities["minFrequencyHz"] = Json(profile.minFrequencyHz);
    capabilities["maxFrequencyHz"] = Json(profile.maxFrequencyHz);
    capabilities["minSweepPoints"] = Json(VnaService::MIN_SWEEP_POINTS);
    capabilities["maxSweepPoints"] = Json(VnaService::MAX_SWEEP_POINTS);
    capabilities["minPowerDbm"] = Json(VnaCore::MIN_POWER_DBM);
    capabilities["maxPowerDbm"] = Json(VnaCore::MAX_POWER_DBM);
    capabilities["manualSweepTimeSupported"] = Json(core.manualSweepTimeSupported);
    capabilities["maxSweepTimeSeconds"] = Json(VnaService::MAX_SWEEP_TIME_SECONDS);
    return capabilities;
}

Json Measurements(const VnaService::InstrumentSnapshot &snapshot)
{
    Json measurements = Json::MakeArray();
    for (const auto &measurement : snapshot.settings.measurements) {
        const auto channel =
            std::find_if(snapshot.channels.begin(), snapshot.channels.end(), [&measurement](const auto &item) {
                return item.id == measurement.channelId;
            });
        measurements.array.push_back(MeasurementJson(
            measurement, channel == snapshot.channels.end() ? VnaCalibration::CalibrationPtr{} : channel->correction));
    }
    return measurements;
}

Json ChannelStatus(const VnaService::InstrumentChannelSnapshot &channel, Json config)
{
    auto item = Json::MakeObject();
    item["channelId"] = Json(channel.id);
    item["name"] = Json(channel.name);
    item["state"] = Json(CoreStateName(channel.core.state));
    item["error"] = Json(channel.core.error);
    item["acquisitionRevision"] = Json(std::to_string(channel.settings.acquisitionRevision));
    item["runId"] = Json(std::to_string(channel.core.runId));
    item["activeConfigValid"] =
        Json(channel.core.state != VnaCore::CoreState::FAULTED && channel.core.state != VnaCore::CoreState::CLOSED &&
             channel.core.configRevision != 0U && channel.core.configRevision == channel.settings.acquisitionRevision);
    item["config"] = std::move(config);
    item["calibration"] = CalibrationJson::Snapshot(channel.calibration);
    item["stimulusZoom"] = StimulusZoomJson::Status(channel.stimulusZoom);
    item["lastSweepTimeSeconds"] = Json(channel.core.lastSweepTimeSeconds);
    return item;
}

}  // 匿名命名空间

HttpResponse HttpApi::Status(const HttpRequest &request) const
{
    const auto snapshot = service_.Snapshot();
    const auto &settings = snapshot.settings;
    HttpResponse response;
    Json &value = response.json = Json::MakeObject();
    value["identity"] = Json(service_.Identity());
    value["backend"] = Json(service_.IsSimulator() ? "NativeSimulator" : "HardwareDriver");
    value["capabilities"] = Capabilities(service_.Profile(), snapshot.core);
    value["state"] = Json(CoreStateName(snapshot.core.state));
    value["error"] = Json(snapshot.core.error);
    value["lastSweepTimeSeconds"] = Json(snapshot.core.lastSweepTimeSeconds);
    value["pending"] = Json(snapshot.pending);
    value["revision"] = Json(std::to_string(settings.revision));
    value["acquisitionRevision"] = Json(std::to_string(settings.acquisitionRevision));
    value["runId"] = Json(std::to_string(snapshot.core.runId));
    value["activeConfigValid"] =
        Json(snapshot.core.state != VnaCore::CoreState::FAULTED && snapshot.core.state != VnaCore::CoreState::CLOSED &&
             snapshot.core.configRevision != 0U && snapshot.core.configRevision == settings.acquisitionRevision);
    value["config"] = ConfigurationJson(settings);
    value["calibration"] = CalibrationJson::Snapshot(snapshot.calibration);
    value["measurements"] = Measurements(snapshot);
    value["display"] = DisplayJson::State(snapshot.display, settings.measurements);
    value["stimulusZoom"] = StimulusZoomJson::Status(snapshot.stimulusZoom);
    value["activeChannelId"] = Json(snapshot.activeChannelId);
    auto channels = Json::MakeArray();
    for (const auto &channel : snapshot.channels) {
        auto item = ChannelStatus(channel, ConfigurationJson(channel.settings));
        channels.array.push_back(std::move(item));
    }
    value["channels"] = std::move(channels);
    value["control"] = ControlJson(snapshot, request);
    return response;
}

}  // 命名空间 VnaWeb
