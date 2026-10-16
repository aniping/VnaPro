/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_protocol_test_support.h"
#include "web/calibration_binary.h"
#include "web/calibration_file_json.h"
#include "web/json_encoder.h"

namespace VnaCalibrationProtocolTest {
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;

void VerifyFileEnvelope(const HttpResponse &exported, const std::string &format)
{
    Require(
        exported.status == 200U && Field(exported.json, "format").string == format &&
            Field(exported.json, "encoding").string == (format == "binary" ? "base64" : "utf8") &&
            Field(exported.json, "filename").string.find(format == "binary" ? ".vcal" : ".csv") != std::string::npos &&
            Field(exported.json, "byteLength").number > 0.,
        "HTTP file export supplies the explicit format, encoding and filename");
}

void VerifyFileImportFailures(
    HttpApi &api, const std::string &token, const std::string &format, const Json &body, const Json &after)
{
    auto conflict = body;
    conflict["content"] = Json(Field(body, "content").string + "changed");
    Require(Request(api, "/api/v1/calibration", conflict, token).status == 409U,
        "A file requestId cannot be reused with changed bytes");
    auto stale = body;
    stale["requestId"] = Json("file-stale-" + format);
    Require(Request(api, "/api/v1/calibration", stale, token).status == 409U,
        "A new file import must still satisfy calibration CAS");
    auto malformed = Action(api, "importFile", "file-malformed-" + format);
    malformed["format"] = Json(format);
    malformed["encoding"] = Field(body, "encoding");
    malformed["content"] = Json("!");
    Require(Request(api, "/api/v1/calibration", malformed, token).status == 400U,
        "Malformed binary/CSV cannot enter the calibration operation queue");
    auto mismatch = Action(api, "importFile", "file-encoding-" + format);
    mismatch["format"] = Json(format);
    mismatch["encoding"] = Json(format == "binary" ? "utf8" : "base64");
    mismatch["content"] = Field(body, "content");
    Require(Request(api, "/api/v1/calibration", mismatch, token).status == 400U &&
                VnaWeb::StringifyJson(Request(api, "/api/v1/calibration").json) == VnaWeb::StringifyJson(after),
        "CAS, conflict and file failures leave the entire calibration state unchanged");
}

void RejectRemovedFileRoutes(HttpApi &api, const std::string &token, const std::string &setId)
{
    Require(Request(api, "/api/v1/calibration/sets/" + setId + "?format=pcs").status == 400U &&
                Request(api, "/api/v1/calibration/sets/" + setId + "?format=binary&format=csv").status == 400U,
        "Unknown or ambiguous file format queries are rejected");
    const auto before = Request(api, "/api/v1/calibration").json;
    auto removed = Action(api, "import", "removed-json-import");
    removed["data"] = Json::MakeObject();
    Require(Request(api, "/api/v1/calibration", removed, token).status == 400U &&
                VnaWeb::StringifyJson(Request(api, "/api/v1/calibration").json) == VnaWeb::StringifyJson(before),
        "The removed JSON file import route must stay unsupported and cannot mutate calibration");
}

void FileTransportsPreserveWriteContract(HttpApi &api, const std::string &token, const std::string &setId)
{
    const auto original = VnaWeb::CalibrationFileJson::Import(Request(api, "/api/v1/calibration/sets/" + setId).json);
    for (const std::string format : {"binary", "csv"}) {
        const auto exported = Request(api, "/api/v1/calibration/sets/" + setId + "?format=" + format);
        VerifyFileEnvelope(exported, format);
        const auto before = Request(api, "/api/v1/calibration").json;
        auto body = Action(api, "importFile", "file-import-" + format);
        for (const auto *field : {"format", "encoding", "content"}) {
            body[field] = Field(exported.json, field);
        }
        Require(Request(api, "/api/v1/calibration", body).status == 403U &&
                    VnaWeb::StringifyJson(Request(api, "/api/v1/calibration").json) == VnaWeb::StringifyJson(before),
            "File import cannot bypass controller ownership");
        const auto accepted = Request(api, "/api/v1/calibration", body, token);
        Complete(api, accepted, token);
        const auto after = Request(api, "/api/v1/calibration").json;
        Require(Field(after, "sets").array.size() == Field(before, "sets").array.size() + 1U &&
                    Field(after, "selectedSetId").string == Field(before, "selectedSetId").string &&
                    Field(after, "enabled").boolean == Field(before, "enabled").boolean,
            "File import appends once and cannot activate or disable the current correction");
        const auto retry = Request(api, "/api/v1/calibration", body, token);
        Require(retry.status == 202U &&
                    Field(retry.json, "operationId").string == Field(accepted.json, "operationId").string,
            "Exact importFile retry precedes its stale revision and reuses the operation");
        const auto importedId = Field(Field(after, "sets").array.back(), "id").string;
        const auto roundtrip =
            VnaWeb::CalibrationFileJson::Import(Request(api, "/api/v1/calibration/sets/" + importedId).json);
        auto normalized = *roundtrip;
        normalized.id = original->id;
        Require(VnaWeb::CalibrationBinary::Encode(normalized) == VnaWeb::CalibrationBinary::Encode(*original),
            "File transport preserves all metadata and coefficients through default binary export");
        VerifyFileImportFailures(api, token, format, body, after);
    }
    RejectRemovedFileRoutes(api, token, setId);
}

}  // 命名空间 VnaCalibrationProtocolTest
