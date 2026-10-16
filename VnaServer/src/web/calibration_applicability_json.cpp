/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/calibration_applicability_json.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "web/calibration_json_fields.h"

namespace VnaWeb {
namespace {

const char *FrequencyMode(VnaService::CalibrationFrequencyMode mode)
{
    using Mode = VnaService::CalibrationFrequencyMode;
    switch (mode) {
        case Mode::NONE:
            return "none";
        case Mode::EXACT:
            return "exact";
        case Mode::SUBSET:
            return "subset";
        case Mode::INTERPOLATED:
            return "interpolated";
    }
    throw std::invalid_argument("Unsupported calibration frequency mode");
}

const char *ReasonCode(VnaService::CalibrationApplicabilityReason reason)
{
    using Reason = VnaService::CalibrationApplicabilityReason;
    switch (reason) {
        case Reason::NONE:
            return "none";
        case Reason::NO_SELECTION:
            return "noSelection";
        case Reason::CONFIGURATION_NOT_READY:
            return "configurationNotReady";
        case Reason::DEVICE_MISMATCH:
            return "deviceMismatch";
        case Reason::INVALID_AXIS:
            return "invalidAxis";
        case Reason::OUT_OF_RANGE:
            return "outOfRange";
        case Reason::POWER_MISMATCH:
            return "powerMismatch";
        case Reason::INVALID_COEFFICIENTS:
            return "invalidCoefficients";
    }
    throw std::invalid_argument("Unsupported calibration applicability reason");
}

const char *WarningName(VnaService::CalibrationWarning warning)
{
    switch (warning) {
        case VnaService::CalibrationWarning::IF_BANDWIDTH_CHANGED:
            return "ifbwChanged";
        case VnaService::CalibrationWarning::INTERPOLATION:
            return "interpolation";
    }
    throw std::invalid_argument("Unsupported calibration warning");
}

}  // 匿名命名空间

Json EncodeCalibrationApplicability(const VnaService::CalibrationApplicability &applicability)
{
    Json value = Json::MakeObject();
    value["applicable"] = Json(applicability.applicable);
    value["prepared"] = Json(applicability.prepared);
    value["frequencyMode"] = Json(FrequencyMode(applicability.frequencyMode));
    value["reasonCode"] = Json(ReasonCode(applicability.reasonCode));
    value["reason"] = Json(applicability.reason);
    value["sourcePoints"] = Json(applicability.sourcePoints);
    value["targetPoints"] = Json(applicability.targetPoints);
    value["exactPoints"] = Json(applicability.exactPoints);
    value["interpolatedPoints"] = Json(applicability.interpolatedPoints);
    value["targetStartHz"] = Json(applicability.targetStartHz);
    value["targetStopHz"] = Json(applicability.targetStopHz);
    value["targetIfBandwidthHz"] = Json(applicability.targetIfBandwidthHz);
    value["targetMinPowerDbm"] = Json(applicability.targetMinPowerDbm);
    value["targetMaxPowerDbm"] = Json(applicability.targetMaxPowerDbm);
    Json warnings = Json::MakeArray();
    for (const auto warning : applicability.warnings) {
        warnings.array.emplace_back(WarningName(warning));
    }
    value["warnings"] = std::move(warnings);
    return value;
}

Json EncodeCalibrationSetSummary(const VnaService::CalibrationSetSummary &set)
{
    Json value = Json::MakeObject();
    value["id"] = Json(std::to_string(set.id));
    value["name"] = Json(set.name);
    value["method"] = Json(CalibrationJsonDetail::MethodName(set.method));
    value["port1"] = Json(set.port1);
    value["port2"] = Json(set.port2);
    value["compatible"] = Json(set.compatible);
    value["includeIsolation"] = Json(set.includeIsolation);
    value["startHz"] = Json(set.startHz);
    value["stopHz"] = Json(set.stopHz);
    value["points"] = Json(set.points);
    value["ifBandwidthHz"] = Json(set.ifBandwidthHz);
    value["powerMinDbm"] = Json(set.powerMinDbm);
    value["powerMaxDbm"] = Json(set.powerMaxDbm);
    value["deviceIdentity"] = Json(set.deviceIdentity);
    value["applicability"] = EncodeCalibrationApplicability(set.applicability);
    return value;
}

}  // 命名空间 VnaWeb
