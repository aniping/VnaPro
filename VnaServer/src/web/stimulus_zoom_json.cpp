/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/stimulus_zoom_json.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace VnaWeb {
namespace {
std::string Text(const Json &body, const std::string &field)
{
    const auto *item = body.Find(field);
    if (item == nullptr || item->type != Json::Type::STRING || item->string.empty()) {
        throw std::invalid_argument(field + " must be a non-empty string");
    }
    return item->string;
}

double Number(const Json &body, const std::string &field)
{
    const auto *item = body.Find(field);
    if (item == nullptr || item->type != Json::Type::NUMBER || !std::isfinite(item->number)) {
        throw std::invalid_argument(field + " must be finite numeric data");
    }
    return item->number;
}
}  // 匿名命名空间

VnaService::StimulusZoomChange StimulusZoomJson::Change(const Json &body)
{
    if (body.type != Json::Type::OBJECT) {
        throw std::invalid_argument("Stimulus zoom body must be an object");
    }
    const auto action = Text(body, "action");
    if (action != "apply" && action != "out") {
        throw std::invalid_argument("Stimulus zoom action must be apply or out");
    }
    std::vector<std::string> allowed{"requestId", "expectedRevision", "expectedDisplayRevision", "action", "channelId"};
    if (action == "apply") {
        allowed.insert(allowed.end(), {"traceName", "startHz", "stopHz", "min", "max"});
    }
    for (const auto &member : body.object) {
        if (std::find(allowed.begin(), allowed.end(), member.first) == allowed.end()) {
            throw std::invalid_argument("Unknown stimulus zoom member: " + member.first);
        }
    }
    VnaService::StimulusZoomChange change;
    if (action == "out") {
        change.action = VnaService::StimulusZoomAction::OUT;
        return change;
    }
    change.traceName = Text(body, "traceName");
    change.startHz = Number(body, "startHz");
    change.stopHz = Number(body, "stopHz");
    change.min = Number(body, "min");
    change.max = Number(body, "max");
    return change;
}

Json StimulusZoomJson::Status(const VnaService::StimulusZoomStatus &status)
{
    auto value = Json::MakeObject();
    value["available"] = Json(status.available);
    value["canZoomOut"] = Json(status.canZoomOut);
    value["reason"] = Json(status.reason);
    value["depth"] = Json(status.depth);
    return value;
}

}  // 命名空间 VnaWeb
