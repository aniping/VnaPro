/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_file_json.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "web/calibration_json.h"

namespace {
void Require(bool value, const char *message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaService::CalibrationSet Fixture()
{
    VnaService::CalibrationSet set;
    set.id = 9007199254740993ULL;
    set.name = "file transport";
    set.deviceIdentity = "file transport fixture";
    set.ifBandwidthHz = 1000.0;
    set.powerDbm = {-10.125, -9.75, -0.0};
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = VnaCalibration::CalibrationMethod::ONE_PORT;
    data->port1 = 4U;
    data->frequencyHz = {10000000.0, 11000000.0, 12000000.0};
    data->onePortTerms.resize(3U);
    for (auto &terms : data->onePortTerms) {
        terms.directivity = {0.02, 0.01};
        terms.sourceMatch = {0.03, -0.02};
        terms.reflectionTracking = {0.9, -0.04};
    }
    set.data = data;
    return set;
}

void Reject(const VnaWeb::Json &body, const char *fragment)
{
    try {
        VnaWeb::CalibrationFileJson::Import(body);
    } catch (const std::exception &error) {
        Require(std::string(error.what()).find(fragment) != std::string::npos, "Unexpected file rejection");
        return;
    }
    throw std::runtime_error("Invalid transport was accepted");
}
}  // 匿名命名空间

int main()
{
    try {
        const auto set = Fixture();
        for (const std::string format : {"binary", "csv"}) {
            const auto exported = VnaWeb::CalibrationFileJson::Export(set, format);
            Require(exported.Find("format")->string == format, "Format must remain explicit");
            Require(exported.Find("filename")->string.find("9007199254740993") != std::string::npos,
                "Filename must retain the uint64 id");
            auto command = VnaWeb::Json::MakeObject();
            command["action"] = VnaWeb::Json("importFile");
            for (const char *key : {"format", "encoding", "content"}) {
                command[key] = *exported.Find(key);
            }
            const auto change = VnaWeb::CalibrationJson::Change(command);
            Require(change.kind == VnaService::CalibrationChangeKind::IMPORT_SET, "File uses shared import path");
            Require(change.importedSet->id == set.id && change.importedSet->powerDbm == set.powerDbm &&
                        change.importedSet->data->port1 == 4U,
                "Transport must preserve full metadata");
            command["encoding"] = VnaWeb::Json(format == "binary" ? "utf8" : "base64");
            Reject(command, "do not match");
        }
        auto invalid = VnaWeb::Json::MakeObject();
        invalid["format"] = VnaWeb::Json("binary");
        invalid["encoding"] = VnaWeb::Json("base64");
        for (const std::string text : {"A", "AB==", "AAF=", "AA==AAAA", "AA?="}) {
            invalid["content"] = VnaWeb::Json(text);
            Reject(invalid, "Base64");
        }
        std::cout << "Calibration file transport passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
