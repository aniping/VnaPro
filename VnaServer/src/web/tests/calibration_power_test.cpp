/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vna_service/calibration_workspace.h"
#include "web/calibration_json.h"
#include "web/json_encoder.h"
#include "web/json_parser.h"

namespace {
using VnaWeb::Json;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaService::CalibrationSet MakeSet()
{
    VnaService::CalibrationSet set;
    set.id = (std::numeric_limits<std::uint64_t>::max)();
    set.name = "Signed fractional dBm";
    set.deviceIdentity = "PowerContract";
    set.ifBandwidthHz = 1000.0;
    set.powerDbm = {-120.0, -12.375, 100.0};
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->frequencyHz = {10000000.0, 20000000.0, 30000000.0};
    data->onePortTerms.assign(3U, {{0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}});
    set.data = std::move(data);
    return set;
}

VnaService::CalibrationSetPtr Import(const Json &document)
{
    auto request = Json::MakeObject();
    request["action"] = Json("import");
    request["data"] = document;
    return VnaWeb::CalibrationJson::Change(request).importedSet;
}

void Reject(const Json &document, const std::string &message)
{
    try {
        Import(document);
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error(message);
}

void NumericSchemaRoundTrip()
{
    const auto set = MakeSet();
    const auto document = VnaWeb::CalibrationJson::Export(set);
    Require(document.Find("schema")->number == 2.0 && !document.Find("powerRaw"), "Export uses schema 2 dBm");
    Require(document.Find("id")->string == "18446744073709551615", "ID remains an exact decimal string");
    const auto encoded = VnaWeb::StringifyJson(document);
    const auto decoded = VnaWeb::ParseJson(encoded);
    const auto imported = Import(decoded);
    Require(imported->powerDbm == set.powerDbm, "Signed fractional dBm and both limits survive JSON roundtrip");
    for (const auto &power : decoded.Find("powerDbm")->array) {
        Require(power.type == Json::Type::NUMBER, "Every schema 2 power entry is numeric");
    }
}

void InvalidPowerRejectedByJson()
{
    const std::vector<Json> invalid{Json(-120.0001), Json(100.0001), Json(std::numeric_limits<double>::quiet_NaN()),
        Json(std::numeric_limits<double>::infinity()), Json(-std::numeric_limits<double>::infinity()), Json("-12.375"),
        Json(true), Json()};
    for (const auto &power : invalid) {
        auto document = VnaWeb::CalibrationJson::Export(MakeSet());
        document["powerDbm"].array[1U] = power;
        Reject(document, "JSON import accepted invalid dBm");
    }
    auto document = VnaWeb::CalibrationJson::Export(MakeSet());
    document["powerDbm"].array.pop_back();
    Reject(document, "JSON import accepted a truncated power axis");
    document = VnaWeb::CalibrationJson::Export(MakeSet());
    document["powerRaw"] = Json::MakeArray();
    Reject(document, "Schema 2 accepted an ambiguous legacy field");
}

Json LegacyDocument()
{
    auto document = VnaWeb::CalibrationJson::Export(MakeSet());
    document["schema"] = Json(1U);
    document.object.erase("powerDbm");
    auto powers = Json::MakeArray();
    powers.array.assign(3U, Json("0"));
    document["powerRaw"] = std::move(powers);
    return document;
}

void LegacyZeroOnlyMigration()
{
    const auto migrated = Import(LegacyDocument());
    Require(migrated->powerDbm == std::vector<double>(3U, 0.0), "All-zero legacy power preserves 0 dBm");
    Require(VnaWeb::CalibrationJson::Export(*migrated).Find("schema")->number == 2.0,
        "Migrated legacy sets export the current schema");
    const std::vector<Json> invalid{
        Json("1"), Json("100"), Json("18446744073709551615"), Json("00"), Json("-0"), Json("0.0"), Json(0U)};
    for (const auto &power : invalid) {
        auto document = LegacyDocument();
        document["powerRaw"].array[1U] = power;
        Reject(document, "Legacy nonzero or noncanonical power must require recalibration");
    }
    auto document = LegacyDocument();
    document["powerDbm"] = Json::MakeArray();
    Reject(document, "Legacy schema accepted both power representations");
}

void ServiceImportValidatesPowerAndCompatibility()
{
    const auto saved = MakeSet();
    VnaService::InstrumentProfile profile;
    const std::vector<double> invalid{-120.0001, 100.0001, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    VnaService::CalibrationWorkspace workspace;
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    std::string error;
    for (double power : invalid) {
        auto bad = std::make_shared<VnaService::CalibrationSet>(saved);
        bad->powerDbm[1U] = power;
        change.importedSet = std::move(bad);
        Require(!workspace.ApplySaved(change, profile, error) && workspace.Revision() == 1U,
            "Direct service import rejects invalid power without mutation");
    }
    change.importedSet = std::make_shared<const VnaService::CalibrationSet>(saved);
    Require(workspace.ApplySaved(change, profile, error), "Service accepts finite signed and fractional dBm");
    VnaCore::SweepPlan plan;
    plan.frequencyHz = saved.data->frequencyHz;
    plan.ifBandwidthRaw = 1000U;
    plan.powerDbm = saved.powerDbm;
    Require(workspace.Snapshot(saved.deviceIdentity, plan).sets.front().compatible, "Same dBm axis is compatible");
    plan.powerDbm[1U] += 0.125;
    Require(!workspace.Snapshot(saved.deviceIdentity, plan).sets.front().compatible,
        "A fractional physical power change invalidates calibration");
}

void SessionFreezesPowerAxis()
{
    VnaService::CalibrationWorkspace workspace;
    VnaService::CalibrationChange begin;
    VnaCore::SweepPlan plan;
    plan.frequencyHz = MakeSet().data->frequencyHz;
    plan.ifBandwidthRaw = 1000U;
    plan.powerDbm.assign(3U, -12.375);
    std::string error;
    Require(workspace.Begin(begin, "PowerContract", plan, VnaService::InstrumentProfile{}, error), error);
    Require(workspace.SessionCompatible("PowerContract", plan), "Session freezes negative fractional power");
    plan.powerDbm[0U] = -12.5;
    Require(!workspace.SessionCompatible("PowerContract", plan), "Changing caller plan cannot change frozen power");
}
}  // 匿名命名空间

int main()
{
    try {
        NumericSchemaRoundTrip();
        InvalidPowerRejectedByJson();
        LegacyZeroOnlyMigration();
        ServiceImportValidatesPowerAndCompatibility();
        SessionFreezesPowerAxis();
        std::cout << "PASS calibration dBm schema, legacy migration and service stimulus contract\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
