/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/calibration_json.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "vna_service/instrument_profile.h"
#include "web/calibration_json_fields.h"
#include "web/display_json.h"

namespace VnaWeb {
namespace CalibrationJsonDetail {
const Json &Field(const Json &object, const std::string &name, Json::Type type)
{
    const auto *value = object.Find(name);
    if (value == nullptr || value->type != type) {
        throw std::invalid_argument("Invalid calibration field: " + name);
    }
    return *value;
}

void Fields(const Json &object, const std::vector<std::string> &allowed)
{
    if (object.type != Json::Type::OBJECT) {
        throw std::invalid_argument("Calibration document must be an object");
    }
    for (const auto &item : object.object) {
        if (std::find(allowed.begin(), allowed.end(), item.first) == allowed.end()) {
            throw std::invalid_argument("Unknown calibration field: " + item.first);
        }
    }
}

std::string Text(const Json &body, const std::string &name)
{
    return Field(body, name, Json::Type::STRING).string;
}

double Number(const Json &body, const std::string &name)
{
    const double value = Field(body, name, Json::Type::NUMBER).number;
    if (!std::isfinite(value)) {
        throw std::invalid_argument(name + " must be finite");
    }
    return value;
}

unsigned Port(const Json &body, const std::string &name, bool zero)
{
    const double value = Number(body, name);
    if (std::floor(value) != value || value < (zero ? 0.0 : 1.0) || value > 4.0) {
        throw std::invalid_argument(name + " must be an instrument port number");
    }
    return static_cast<unsigned>(value);
}

VnaCalibration::CalibrationMethod Method(const std::string &method)
{
    if (method == "onePort") {
        return VnaCalibration::CalibrationMethod::ONE_PORT;
    }
    if (method == "twoPort") {
        return VnaCalibration::CalibrationMethod::TWO_PORT;
    }
    throw std::invalid_argument("Unsupported calibration method");
}

const char *MethodName(VnaCalibration::CalibrationMethod method)
{
    return method == VnaCalibration::CalibrationMethod::ONE_PORT ? "onePort" : "twoPort";
}

const char *StandardName(VnaService::CalibrationStandard standard)
{
    switch (standard) {
        case VnaService::CalibrationStandard::OPEN:
            return "open";
        case VnaService::CalibrationStandard::SHORT:
            return "short";
        case VnaService::CalibrationStandard::LOAD:
            return "load";
        case VnaService::CalibrationStandard::THRU:
            return "thru";
        case VnaService::CalibrationStandard::ISOLATION:
            return "isolation";
        default:
            throw std::invalid_argument("Unknown calibration standard");
    }
}

}  // 命名空间 CalibrationJsonDetail

using namespace CalibrationJsonDetail;

Json CalibrationJson::Snapshot(const VnaService::CalibrationSnapshot &snapshot)
{
    Json value = Json::MakeObject();
    value["revision"] = Json(std::to_string(snapshot.revision));
    value["collecting"] = Json(snapshot.collecting);
    value["capturePending"] = Json(snapshot.capturePending);
    value["sessionId"] = Json(std::to_string(snapshot.sessionId));
    value["method"] = Json(MethodName(snapshot.method));
    value["port1"] = Json(snapshot.port1);
    value["port2"] = Json(snapshot.port2);
    value["includeIsolation"] = Json(snapshot.includeIsolation);
    value["selectedSetId"] = Json(std::to_string(snapshot.selectedSetId));
    value["enabled"] = Json(snapshot.enabled);
    value["compatible"] = Json(snapshot.compatible);
    value["reason"] = Json(snapshot.reason);
    value["error"] = Json(snapshot.error);
    Json steps = Json::MakeArray();
    for (const auto &step : snapshot.steps) {
        Json item = Json::MakeObject();
        item["id"] = Json(step.id);
        item["kind"] = Json(StandardName(step.kind));
        item["port1"] = Json(step.port1);
        item["port2"] = Json(step.port2);
        item["captured"] = Json(step.captured);
        item["runId"] = Json(std::to_string(step.runId));
        item["frameSeq"] = Json(std::to_string(step.frameSeq));
        steps.array.push_back(std::move(item));
    }
    value["steps"] = std::move(steps);
    Json sets = Json::MakeArray();
    for (const auto &set : snapshot.sets) {
        Json item = Json::MakeObject();
        item["id"] = Json(std::to_string(set.id));
        item["name"] = Json(set.name);
        item["method"] = Json(MethodName(set.method));
        item["port1"] = Json(set.port1);
        item["port2"] = Json(set.port2);
        item["compatible"] = Json(set.compatible);
        item["includeIsolation"] = Json(set.includeIsolation);
        item["startHz"] = Json(set.startHz);
        item["stopHz"] = Json(set.stopHz);
        item["points"] = Json(set.points);
        item["ifBandwidthHz"] = Json(set.ifBandwidthHz);
        sets.array.push_back(std::move(item));
    }
    value["sets"] = std::move(sets);
    return value;
}

VnaService::CalibrationChange CalibrationJson::Change(const Json &body)
{
    const std::string action = Text(body, "action");
    std::vector<std::string> allowed{"requestId", "expectedRevision", "action"};
    VnaService::CalibrationChange change;
    using Kind = VnaService::CalibrationChangeKind;
    if (action == "begin") {
        allowed.insert(allowed.end(), {"method", "port1", "port2", "includeIsolation"});
        change.kind = Kind::BEGIN;
        change.method = Method(Text(body, "method"));
        change.port1 = Port(body, "port1");
        if (body.Find("port2")) {
            change.port2 = Port(body, "port2", true);
        }
        if (body.Find("includeIsolation")) {
            change.includeIsolation = Field(body, "includeIsolation", Json::Type::BOOLEAN).boolean;
        }
    } else if (action == "capture" || action == "finish" || action == "cancel") {
        allowed.push_back("sessionId");
        change.sessionId = DisplayJson::Revision(body, "sessionId");
        if (change.sessionId == 0U) {
            throw std::invalid_argument("A collecting calibration session is required");
        }
        if (action == "capture") {
            allowed.push_back("stepId");
            change.kind = Kind::CAPTURE;
            change.stepId = Text(body, "stepId");
        } else if (action == "finish") {
            allowed.push_back("name");
            change.kind = Kind::FINISH;
            change.name = body.Find("name") ? Text(body, "name") : "";
        } else {
            change.kind = Kind::CANCEL;
        }
    } else if (action == "enable") {
        allowed.push_back("enabled");
        change.kind = Kind::ENABLE;
        change.enabled = Field(body, "enabled", Json::Type::BOOLEAN).boolean;
    } else if (action == "select" || action == "delete") {
        allowed.push_back("setId");
        change.kind = action == "select" ? Kind::SELECT : Kind::DELETE_SET;
        change.setId = DisplayJson::Revision(body, "setId");
    } else if (action == "import") {
        allowed.push_back("data");
        change.kind = Kind::IMPORT_SET;
        change.importedSet = Import(Field(body, "data", Json::Type::OBJECT));
    } else {
        throw std::invalid_argument("Unsupported calibration action");
    }
    Fields(body, allowed);
    return change;
}

}  // 命名空间 VnaWeb
