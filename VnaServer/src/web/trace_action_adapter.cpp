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
    const bool matrix = path == "/api/v1/measurement-actions/all-sparameters";
    if (!clone && !add && !remove && !matrix) {
        throw std::invalid_argument("Unknown measurement action");
    }
    std::vector<std::string> allowed{"requestId", "expectedRevision", "expectedDisplayRevision"};
    if (matrix) {
        allowed.push_back("confirmed");
    } else {
        allowed.push_back("diagram");
    }
    if (!remove && !matrix) {
        allowed.push_back("newDiagram");
        allowed.push_back("position");
    }
    if (clone) {
        allowed.push_back("sourceName");
    } else if (add) {
        allowed.push_back("parameter");
    }
    for (const auto &member : body.object) {
        if (std::find(allowed.begin(), allowed.end(), member.first) == allowed.end()) {
            throw std::invalid_argument("Unknown measurement action field: " + member.first);
        }
    }
    const auto revision = DisplayJson::Revision(body, "expectedRevision");
    const auto displayRevision = DisplayJson::Revision(body, "expectedDisplayRevision");
    if (matrix) {
        const auto *confirmed = body.Find("confirmed");
        if (confirmed == nullptr || confirmed->type != Json::Type::BOOLEAN || !confirmed->boolean) {
            throw std::invalid_argument("Replacing all measurement definitions requires confirmed: true");
        }
        return service.CreateAllSParameters(revision, displayRevision, owner);
    }
    if (remove) {
        return service.DeleteDiagram(Diagram(body, true), revision, displayRevision, owner);
    }
    VnaService::TraceCreationOptions options;
    options.diagram = Diagram(body, false);
    options.newDiagram = NewDiagram(body);
    const auto *position = body.Find("position");
    if (position != nullptr && (position->type != Json::Type::STRING ||
                                   !VnaDisplay::ParseTraceDropPosition(position->string, options.position))) {
        throw std::invalid_argument("position must be center, above, below, left or right");
    }
    if (options.position != VnaDisplay::TraceDropPosition::CENTER && (options.diagram == 0U || options.newDiagram)) {
        throw std::invalid_argument("Relative split requires diagram and cannot combine newDiagram");
    }
    if (add) {
        const auto *parameter = body.Find("parameter");
        if (parameter != nullptr) {
            if (parameter->type != Json::Type::STRING || parameter->string.empty()) {
                throw std::invalid_argument("parameter must be a non-empty string");
            }
            options.parameter = parameter->string;
        }
        return service.AddMeasurement(revision, displayRevision, owner, options);
    }
    const auto *source = body.Find("sourceName");
    if (source != nullptr && source->type != Json::Type::STRING) {
        throw std::invalid_argument("sourceName must be a string");
    }
    return service.CloneMeasurement(source == nullptr ? "" : source->string, revision, displayRevision, owner, options);
}

}  // 命名空间 VnaWeb
