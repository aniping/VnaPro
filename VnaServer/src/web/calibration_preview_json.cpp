/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/calibration_preview_json.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include "vna_display/trace_projection.h"
#include "vna_service/instrument_profile.h"
#include "web/display_json.h"

namespace VnaWeb {
namespace {
constexpr std::size_t MAX_STEP_ID_BYTES = 128U;

Json Project(const VnaService::CalibrationPreviewResult &preview, VnaDisplay::DisplayFormat format,
    const std::vector<std::size_t> &indexes)
{
    VnaDisplay::TracePresentation presentation;
    presentation.name = preview.measurement.name;
    presentation.format = format;
    const auto projected =
        VnaDisplay::TraceProjection::Calculate(presentation, preview.trace, preview.frequencyHz, preview.measurement);
    return DisplayJson::Projection(projected, format, indexes);
}
}  // 匿名命名空间

CalibrationPreviewQuery CalibrationPreviewJson::ParseQuery(const std::string &query)
{
    Json fields = Json::MakeObject();
    for (std::size_t begin = 0U; begin < query.size();) {
        const auto end = query.find('&', begin);
        const auto field = query.substr(begin, end == std::string::npos ? end : end - begin);
        const auto separator = field.find('=');
        const auto key = field.substr(0U, separator);
        if (separator == std::string::npos || (key != "sessionId" && key != "stepId") || fields.Find(key)) {
            throw std::invalid_argument("Calibration preview requires unique sessionId and stepId");
        }
        fields[key] = Json(field.substr(separator + 1U));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1U;
        if (begin == query.size()) {
            throw std::invalid_argument("Empty calibration preview query field");
        }
    }
    const auto step = fields.Find("stepId");
    if (!step || step->string.empty() || step->string.size() > MAX_STEP_ID_BYTES ||
        !std::all_of(step->string.begin(), step->string.end(), [](unsigned char value) {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
                   value == '-' || value == '_';
        })) {
        throw std::invalid_argument("Invalid calibration standard identifier");
    }
    CalibrationPreviewQuery result;
    result.sessionId = DisplayJson::Revision(fields, "sessionId");
    result.stepId = step->string;
    if (result.sessionId == 0U) {
        throw std::invalid_argument("Calibration preview requires an active session identifier");
    }
    return result;
}

Json CalibrationPreviewJson::Encode(const VnaService::CalibrationPreviewResult &preview)
{
    Json result = Json::MakeObject();
    result["available"] = Json(preview.available);
    result["sessionId"] = Json(std::to_string(preview.sessionId));
    result["stepId"] = Json(preview.step.id);
    result["calibrationRevision"] = Json(std::to_string(preview.calibrationRevision));
    if (!preview.available) {
        result["reason"] = Json(preview.error);
        return result;
    }
    const auto count = preview.frequencyHz.size();
    if (!preview.trace.available || count == 0U || count > VnaService::MAX_SWEEP_POINTS ||
        count != preview.trace.points.size()) {
        throw std::logic_error("Saved calibration standard has inconsistent samples");
    }
    Json axis = Json::MakeArray();
    Json samples = Json::MakeArray();
    Json real = Json::MakeArray();
    Json imag = Json::MakeArray();
    Json valid = Json::MakeArray();
    std::vector<std::size_t> indexes;
    indexes.reserve(count);
    for (std::size_t point = 0U; point < count; ++point) {
        indexes.push_back(point);
        axis.array.emplace_back(preview.frequencyHz[point]);
        samples.array.emplace_back(static_cast<unsigned>(point));
        const auto &value = preview.trace.points[point];
        real.array.push_back(value.valid ? Json(value.real) : Json());
        imag.array.push_back(value.valid ? Json(value.imag) : Json());
        valid.array.emplace_back(value.valid);
    }
    result["parameter"] = Json(preview.measurement.Parameter());
    result["runId"] = Json(std::to_string(preview.step.runId));
    result["frameSeq"] = Json(std::to_string(preview.step.frameSeq));
    result["ifBandwidthHz"] = Json(preview.ifBandwidthHz);
    result["originalPointCount"] = Json(static_cast<unsigned>(count));
    result["displayPointCount"] = Json(static_cast<unsigned>(count));
    result["decimated"] = Json(false);
    result["frequencyHz"] = std::move(axis);
    result["sampleIndexes"] = std::move(samples);
    result["real"] = std::move(real);
    result["imag"] = std::move(imag);
    result["valid"] = std::move(valid);
    auto db = Project(preview, VnaDisplay::DisplayFormat::DB, indexes);
    auto phase = Project(preview, VnaDisplay::DisplayFormat::PHASE, indexes);
    result["dbValues"] = std::move(db.object.at("values"));
    result["dbStates"] = std::move(db.object.at("valueStates"));
    result["phaseValues"] = std::move(phase.object.at("values"));
    result["phaseStates"] = std::move(phase.object.at("valueStates"));
    return result;
}
}  // 命名空间 VnaWeb
