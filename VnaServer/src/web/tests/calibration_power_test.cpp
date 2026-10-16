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
#include "web/calibration_binary.h"
#include "web/calibration_csv.h"

namespace {

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaService::CalibrationSet MakeSet()
{
    VnaService::CalibrationSet set;
    set.id = (std::numeric_limits<std::uint64_t>::max)() - 1U;
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

void FilePowerRoundTrip()
{
    const auto set = MakeSet();
    const auto binary = VnaWeb::CalibrationBinary::Decode(VnaWeb::CalibrationBinary::Encode(set));
    const auto csv = VnaWeb::CalibrationCsv::Decode(VnaWeb::CalibrationCsv::Encode(set));
    for (const auto &decoded : {binary, csv}) {
        Require(decoded->id == set.id, "File ID remains exact above double precision");
        Require(decoded->powerDbm == set.powerDbm, "Signed fractional dBm and both limits survive file roundtrip");
    }
    for (const double power : {-120.0001, 100.0001, std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        auto bad = set;
        bad.powerDbm[1U] = power;
        bool rejected = false;
        try {
            VnaWeb::CalibrationBinary::Encode(bad);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        Require(rejected, "Binary export rejects invalid physical power");
    }
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
        FilePowerRoundTrip();
        ServiceImportValidatesPowerAndCompatibility();
        SessionFreezesPowerAxis();
        std::cout << "PASS calibration file dBm and service stimulus contract\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
