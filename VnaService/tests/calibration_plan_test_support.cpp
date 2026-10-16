/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_plan_test_support.h"

#include <stdexcept>
#include <utility>

namespace CalibrationPlanTest {

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaService::CalibrationSetPtr Source(const std::string &identity, unsigned points)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = VnaCalibration::CalibrationMethod::FOUR_PORT;
    data->port1 = 1U;
    data->port2 = 4U;
    for (unsigned point = 0U; point < points; ++point) {
        data->frequencyHz.push_back(10000000.0 + 1000.0 * point);
        VnaCalibration::FourPortTerms terms;
        for (unsigned cell = 0U; cell < terms.size(); ++cell) {
            const double phase = 0.000001 * point;
            terms[cell].offset =
                cell / 4U == cell % 4U ? VnaCalibration::Complex{0.01 + phase, -0.0} : VnaCalibration::Complex{};
            terms[cell].match = {0.001 * cell + phase, -0.002};
            terms[cell].tracking = {0.9 + phase + 0.01 * cell, -0.02 - phase};
        }
        data->fourPortTerms.push_back(terms);
    }
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->id = 1U;
    set->name = "Nonconstant four-port";
    set->deviceIdentity = identity;
    set->ifBandwidthHz = 1000.0;
    set->powerDbm.assign(points, 0.0);
    set->data = std::move(data);
    return set;
}

VnaCore::SweepPlan Plan(const std::vector<double> &axis, double power)
{
    VnaCore::SweepPlan plan;
    plan.frequencyHz = axis;
    plan.powerDbm.assign(axis.size(), power);
    plan.ifBandwidthRaw = 1000U;
    for (double frequency : axis) {
        plan.frequencyRaw.push_back(static_cast<std::uint64_t>(frequency));
    }
    return plan;
}

void Import(VnaService::CalibrationWorkspace &workspace, const VnaService::CalibrationSetPtr &set)
{
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    change.importedSet = set;
    std::string error;
    Require(workspace.ApplySaved(change, VnaService::InstrumentProfile{}, error), error);
}

void Activate(VnaService::CalibrationWorkspace &workspace, std::uint64_t id)
{
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::ACTIVATE;
    change.setId = id;
    std::string error;
    Require(workspace.ApplySaved(change, VnaService::InstrumentProfile{}, error), error);
}
}  // 命名空间 CalibrationPlanTest
