/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <stdexcept>

#include "web/calibration_file_json.h"
#include "web/display_json.h"
#include "web/http_api.h"

namespace VnaWeb {
HttpResponse HttpApi::CalibrationSetFile(const std::string &setId, const std::string &query, unsigned channelId) const
{
    const std::string format = query == "format=binary" ? "binary" : query == "format=csv" ? "csv" : "";
    if (!query.empty() && format.empty()) {
        throw std::invalid_argument("Unknown calibration set query field");
    }
    Json id = Json::MakeObject();
    id["id"] = Json(setId);
    const auto set = service_.FindCalibrationSet(DisplayJson::Revision(id, "id"), channelId);
    if (!set) {
        return HttpError(404, "calibration_not_found", "Calibration set does not exist");
    }
    HttpResponse response;
    response.json = CalibrationFileJson::Export(*set, format.empty() ? "binary" : format);
    return response;
}

}  // 命名空间 VnaWeb
