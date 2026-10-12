/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/trace_action_adapter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "web/display_json.h"

namespace VnaWeb {
namespace {
unsigned Diagram(const Json &body, bool required)
{
    const auto *value = body.Find("diagram");
    if (value == nullptr && !required) {
        return 0U;
    }
    if (value == nullptr || value->type != Json::Type::NUMBER || !std::isfinite(value->number) || value->number < 1.0 ||
        value->number > VnaDisplay::MAX_DISPLAY_DIAGRAMS || std::floor(value->number) != value->number) {
        throw std::invalid_argument("diagram must be an existing positive window number");
    }
    return static_cast<unsigned>(value->number);
}

bool NewDiagram(const Json &body)
{
    const auto *value = body.Find("newDiagram");
    if (value == nullptr) {
        return false;
    }
    if (value->type != Json::Type::BOOLEAN) {
        throw std::invalid_argument("newDiagram must be a boolean");
    }
    return value->boolean;
}
}  // 匿名命名空间

VnaService::Operation TraceActionAdapter::Execute(
    VnaService::InstrumentService &service, const std::string &path, const Json &body, const std::string &owner)
{
    const bool clone = path == "/api/v1/measurement-actions/clone";
    const bool add = path == "/api/v1/measurement-actions/add";
    const bool remove = path == "/api/v1/measurement-actions/delete-diagram";
    if (!clone && !add && !remove) {
        throw std::invalid_argument("Unknown measurement action");
    }
    std::vector<std::string> allowed{"requestId", "expectedRevision", "expectedDisplayRevision", "diagram"};
    if (!remove) {
        allowed.push_back("newDiagram");
    }
    if (clone) {
        allowed.push_back("sourceName");
    }
    for (const auto &member : body.object) {
        if (std::find(allowed.begin(), allowed.end(), member.first) == allowed.end()) {
            throw std::invalid_argument("Unknown measurement action field: " + member.first);
        }
    }
    const auto revision = DisplayJson::Revision(body, "expectedRevision");
    const auto displayRevision = DisplayJson::Revision(body, "expectedDisplayRevision");
    if (remove) {
        return service.DeleteDiagram(Diagram(body, true), revision, displayRevision, owner);
    }
    VnaService::TraceCreationOptions options;
    options.diagram = Diagram(body, false);
    options.newDiagram = NewDiagram(body);
    if (add) {
        return service.AddMeasurement(revision, displayRevision, owner, options);
    }
    const auto *source = body.Find("sourceName");
    if (source != nullptr && source->type != Json::Type::STRING) {
        throw std::invalid_argument("sourceName must be a string");
    }
    return service.CloneMeasurement(source == nullptr ? "" : source->string, revision, displayRevision, owner, options);
}

}  // 命名空间 VnaWeb
