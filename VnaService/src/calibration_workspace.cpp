/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_service/calibration_workspace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "calibration_set_validation.h"
#include "vna_measurement/measurement_processor.h"

namespace VnaService {
namespace {
using VnaCalibration::Complex;

bool Compatible(const CalibrationSet &set, const std::string &identity, const VnaCore::SweepPlan &plan) noexcept
{
    return set.data && set.deviceIdentity == identity && set.ifBandwidthHz == plan.ifBandwidthRaw &&
           set.powerDbm == plan.powerDbm && set.data->frequencyHz == plan.frequencyHz;
}

void AddPort(VnaCore::SweepPlan &plan, unsigned port)
{
    if (port == 0U) {
        return;
    }
    plan.receiverMask |= 1U << (port - 1U);
    if (std::find(plan.sourcePorts.begin(), plan.sourcePorts.end(), port) == plan.sourcePorts.end()) {
        plan.sourcePorts.push_back(port);
    }
}

VnaMeasurement::TraceResult RawTrace(const VnaCore::MeasurementFrame &frame, unsigned receiver, unsigned source)
{
    VnaMeasurement::Measurement measurement;
    measurement.name = "CalibrationStandard";
    measurement.kind = VnaMeasurement::MeasurementKind::S_PARAMETER;
    measurement.receiverKind = 'b';
    measurement.receiverPort = receiver;
    measurement.sourcePort = source;
    return VnaMeasurement::MeasurementProcessor::Calculate(frame, measurement);
}

bool ReadTrace(const VnaCore::MeasurementFrame &frame, unsigned receiver, unsigned source, std::vector<Complex> &values,
    std::string &error)
{
    const auto trace = RawTrace(frame, receiver, source);
    if (!trace.available || trace.points.size() != frame.plan.frequencyHz.size()) {
        error = trace.error.empty() ? "Calibration standard trace is incomplete" : trace.error;
        return false;
    }
    values.reserve(trace.points.size());
    for (const auto &point : trace.points) {
        if (!point.valid) {
            error = "Calibration standard contains an invalid sample or reference wave";
            return false;
        }
        values.emplace_back(point.real, point.imag);
    }
    return true;
}
}  // 匿名命名空间

std::uint64_t CalibrationWorkspace::Revision() const noexcept
{
    return revision_;
}

bool CalibrationWorkspace::Collecting() const noexcept
{
    return collecting_;
}

bool CalibrationWorkspace::SessionMatches(std::uint64_t id) const noexcept
{
    return collecting_ && id != 0U && id == session_.id;
}

bool CalibrationWorkspace::SessionCompatible(const std::string &identity, const VnaCore::SweepPlan &plan) const noexcept
{
    return collecting_ && Compatible(session_, identity, plan);
}

CalibrationSetPtr CalibrationWorkspace::Find(std::uint64_t id) const
{
    for (const auto &set : sets_) {
        if (set->id == id) {
            return set;
        }
    }
    return {};
}

CalibrationPreviewResult CalibrationWorkspace::Preview(std::uint64_t sessionId, const std::string &stepId) const
{
    CalibrationPreviewResult result;
    result.calibrationRevision = revision_;
    result.sessionId = sessionId;
    result.step.id = stepId;
    if (!SessionMatches(sessionId)) {
        result.error = "Calibration preview session is missing or changed";
        return result;
    }
    const auto found = std::find_if(samples_.begin(), samples_.end(), [&stepId](const Sample &sample) {
        return sample.step.id == stepId;
    });
    if (found == samples_.end()) {
        result.error = "Calibration preview standard does not exist";
        return result;
    }
    result.step = found->step;
    if (!found->step.captured || found->network.size() != session_.data->frequencyHz.size()) {
        result.error = "Calibration standard has no confirmed complete measurement";
        return result;
    }
    const bool pair =
        found->step.kind == CalibrationStandard::THRU || found->step.kind == CalibrationStandard::ISOLATION;
    result.frequencyHz = session_.data->frequencyHz;
    result.ifBandwidthHz = session_.ifBandwidthHz;
    result.measurement.name = found->step.id;
    result.measurement.kind = VnaMeasurement::MeasurementKind::S_PARAMETER;
    result.measurement.receiverKind = 'b';
    result.measurement.receiverPort = pair ? port2_ : found->step.port1;
    result.measurement.sourcePort = pair ? port1_ : found->step.port1;
    result.trace.points.reserve(found->network.size());
    for (const auto &point : found->network) {
        const auto value = pair ? point.s21 : point.s11;
        VnaMeasurement::ComplexPoint sample;
        sample.real = value.real();
        sample.imag = value.imag();
        sample.valid = true;
        sample.error = VnaMeasurement::PointError::NONE;
        result.trace.points.push_back(sample);
    }
    result.trace.available = true;
    result.available = true;
    return result;
}

CalibrationSnapshot CalibrationWorkspace::Snapshot(const std::string &identity, const VnaCore::SweepPlan &plan) const
{
    CalibrationSnapshot result;
    result.revision = revision_;
    result.collecting = collecting_;
    result.capturePending = capturePending_;
    result.sessionId = collecting_ ? session_.id : 0U;
    result.method = method_;
    result.port1 = port1_;
    result.port2 = port2_;
    result.includeIsolation = includeIsolation_;
    for (const auto &sample : samples_) {
        result.steps.push_back(sample.step);
    }
    result.selectedSetId = selectedSetId_;
    result.enabled = enabled_;
    result.error = error_;
    const auto selected = Find(selectedSetId_);
    if (!collecting_ && selected) {
        result.method = selected->data->method;
        result.port1 = selected->data->port1;
        result.port2 = selected->data->port2;
        result.includeIsolation = selected->includeIsolation;
    }
    result.compatible = selected && Compatible(*selected, identity, plan);
    result.reason = !selected            ? "No calibration set selected"
                    : !result.compatible ? "Calibration stimulus or device identity does not match"
                    : !enabled_          ? "Correction is disabled"
                                         : "Correction is enabled";
    for (const auto &set : sets_) {
        CalibrationSetSummary summary;
        summary.id = set->id;
        summary.name = set->name;
        summary.method = set->data->method;
        summary.port1 = set->data->port1;
        summary.port2 = set->data->port2;
        summary.includeIsolation = set->includeIsolation;
        summary.compatible = Compatible(*set, identity, plan);
        summary.startHz = set->data->frequencyHz.front();
        summary.stopHz = set->data->frequencyHz.back();
        summary.points = static_cast<unsigned>(set->data->frequencyHz.size());
        summary.ifBandwidthHz = set->ifBandwidthHz;
        result.sets.push_back(std::move(summary));
    }
    return result;
}

VnaCalibration::CalibrationPtr CalibrationWorkspace::Correction(
    const std::string &identity, const VnaCore::SweepPlan &plan) const
{
    const auto set = Find(selectedSetId_);
    return enabled_ && set && Compatible(*set, identity, plan) ? set->data : VnaCalibration::CalibrationPtr{};
}

void CalibrationWorkspace::ExtendPlan(VnaCore::SweepPlan &plan) const
{
    if (collecting_) {
        AddPort(plan, port1_);
        AddPort(plan, port2_);
    }
    const auto set = Find(selectedSetId_);
    if (set) {
        AddPort(plan, set->data->port1);
        AddPort(plan, set->data->port2);
    }
    std::sort(plan.sourcePorts.begin(), plan.sourcePorts.end());
}

bool CalibrationWorkspace::CanRevise(std::string &error) const
{
    if (revision_ == (std::numeric_limits<std::uint64_t>::max)()) {
        error = "Calibration revision exhausted";
        return false;
    }
    return true;
}

void CalibrationWorkspace::AddSteps(unsigned port)
{
    const std::array<CalibrationStandard, 3U> kinds{
        {CalibrationStandard::OPEN, CalibrationStandard::SHORT, CalibrationStandard::LOAD}};
    const std::array<const char *, 3U> names{{"open", "short", "load"}};
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        Sample sample;
        sample.step.id = std::string(names[index]) + "-" + std::to_string(port);
        sample.step.kind = kinds[index];
        sample.step.port1 = port;
        samples_.push_back(std::move(sample));
    }
}

bool CalibrationWorkspace::Begin(const CalibrationChange &change, const std::string &identity,
    const VnaCore::SweepPlan &plan, const InstrumentProfile &profile, std::string &error)
{
    if (!CanRevise(error)) {
        return false;
    }
    if (collecting_ || sets_.size() >= MAX_CALIBRATION_SETS || !Detail::CalibrationSetupValid(change, profile) ||
        nextSessionId_ == (std::numeric_limits<std::uint64_t>::max)()) {
        error = "Calibration session, ports, name or set capacity is invalid";
        return false;
    }
    if (plan.frequencyHz.size() < MIN_SWEEP_POINTS ||
        std::adjacent_find(plan.frequencyHz.begin(), plan.frequencyHz.end(), [](double left, double right) {
            return !std::isfinite(left) || !std::isfinite(right) || left >= right;
        }) != plan.frequencyHz.end()) {
        error = "Calibration requires a strictly increasing physical frequency axis; CW is unsupported";
        return false;
    }
    InitializeSession(change, identity, plan);
    collecting_ = true;
    capturePending_ = false;
    error_.clear();
    ++revision_;
    return true;
}

void CalibrationWorkspace::InitializeSession(
    const CalibrationChange &change, const std::string &identity, const VnaCore::SweepPlan &plan)
{
    VnaCalibration::CalibrationData axis;
    axis.method = change.method;
    axis.port1 = change.port1;
    axis.port2 = change.port2;
    axis.frequencyHz = plan.frequencyHz;
    session_ = {};
    session_.id = nextSessionId_++;
    session_.name = change.name;
    session_.deviceIdentity = identity;
    session_.includeIsolation = change.includeIsolation;
    session_.ifBandwidthHz = plan.ifBandwidthRaw;
    session_.powerDbm = plan.powerDbm;
    session_.data = std::make_shared<const VnaCalibration::CalibrationData>(std::move(axis));
    method_ = change.method;
    port1_ = change.port1;
    port2_ = change.port2;
    includeIsolation_ = change.includeIsolation;
    samples_.clear();
    AddSteps(port1_);
    if (method_ == VnaCalibration::CalibrationMethod::TWO_PORT) {
        AddSteps(port2_);
        Sample thru;
        thru.step.kind = CalibrationStandard::THRU;
        thru.step.port1 = port1_;
        thru.step.port2 = port2_;
        thru.step.id = "thru-" + std::to_string(port1_) + "-" + std::to_string(port2_);
        samples_.push_back(std::move(thru));
        if (includeIsolation_) {
            Sample isolation;
            isolation.step = samples_.back().step;
            isolation.step.kind = CalibrationStandard::ISOLATION;
            isolation.step.id = "isolation-" + std::to_string(port1_) + "-" + std::to_string(port2_);
            samples_.push_back(std::move(isolation));
        }
    }
}

bool CalibrationWorkspace::ValidateStep(std::uint64_t sessionId, const std::string &stepId, std::string &error) const
{
    if (!SessionMatches(sessionId) || capturePending_) {
        error = "Calibration session is missing, changed or already capturing";
        return false;
    }
    const auto found = std::find_if(samples_.begin(), samples_.end(), [&stepId](const Sample &sample) {
        return sample.step.id == stepId;
    });
    if (found == samples_.end()) {
        error = "Calibration standard step does not exist";
        return false;
    }
    return CanRevise(error);
}

void CalibrationWorkspace::SetCapturePending(bool value) noexcept
{
    capturePending_ = value;
}

void CalibrationWorkspace::CaptureFailed(const std::string &error)
{
    capturePending_ = false;
    error_ = error;
}

bool CalibrationWorkspace::Capture(
    const std::string &stepId, const VnaCore::MeasurementFrame &frame, std::string &error)
{
    const auto found = std::find_if(samples_.begin(), samples_.end(), [&stepId](const Sample &sample) {
        return sample.step.id == stepId;
    });
    if (!collecting_ || found == samples_.end() || !CanRevise(error) ||
        !Compatible(session_, session_.deviceIdentity, frame.plan)) {
        error = "Calibration standard does not match the frozen session stimulus";
        return false;
    }
    const bool pair =
        found->step.kind == CalibrationStandard::THRU || found->step.kind == CalibrationStandard::ISOLATION;
    std::vector<Complex> s11, s21, s22, s12;
    const unsigned reflectionPort = pair ? port1_ : found->step.port1;
    if (!ReadTrace(frame, reflectionPort, reflectionPort, s11, error) ||
        (pair && (!ReadTrace(frame, port2_, port1_, s21, error) || !ReadTrace(frame, port2_, port2_, s22, error) ||
                     !ReadTrace(frame, port1_, port2_, s12, error)))) {
        return false;
    }
    std::vector<VnaCalibration::TwoPortNetwork> network(s11.size());
    for (std::size_t point = 0U; point < s11.size(); ++point) {
        network[point].s11 = s11[point];
        if (pair) {
            network[point].s21 = s21[point];
            network[point].s22 = s22[point];
            network[point].s12 = s12[point];
        }
    }
    found->network = std::move(network);
    found->step.captured = true;
    found->step.runId = frame.runId;
    found->step.frameSeq = frame.frameSeq;
    capturePending_ = false;
    error_.clear();
    ++revision_;
    return true;
}

const CalibrationWorkspace::Sample *CalibrationWorkspace::FindSample(
    CalibrationStandard kind, unsigned port) const noexcept
{
    for (const auto &sample : samples_) {
        if (sample.step.kind == kind && sample.step.port1 == port) {
            return &sample;
        }
    }
    return nullptr;
}

bool CalibrationWorkspace::Solve(VnaCalibration::CalibrationData &data, std::string &error) const
{
    const std::array<Complex, 3U> standards{{Complex{1.0, 0.0}, Complex{-1.0, 0.0}, Complex{}}};
    const auto *open = FindSample(CalibrationStandard::OPEN, port1_);
    const auto *shorted = FindSample(CalibrationStandard::SHORT, port1_);
    const auto *load = FindSample(CalibrationStandard::LOAD, port1_);
    const auto *reverseOpen = FindSample(CalibrationStandard::OPEN, port2_);
    const auto *reverseShort = FindSample(CalibrationStandard::SHORT, port2_);
    const auto *reverseLoad = FindSample(CalibrationStandard::LOAD, port2_);
    const auto *thru = FindSample(CalibrationStandard::THRU, port1_);
    const auto *isolation = FindSample(CalibrationStandard::ISOLATION, port1_);
    VnaCalibration::TwoPortNetwork defined;
    defined.s21 = defined.s12 = Complex{1.0, 0.0};
    for (std::size_t point = 0U; point < data.frequencyHz.size(); ++point) {
        VnaCalibration::OnePortTerms forward, reverse;
        const std::array<Complex, 3U> measured{
            {open->network[point].s11, shorted->network[point].s11, load->network[point].s11}};
        auto result = VnaCalibration::SolveThreeStandards(measured, standards, forward);
        if (result == VnaCalibration::CalibrationError::NONE &&
            method_ == VnaCalibration::CalibrationMethod::TWO_PORT) {
            const std::array<Complex, 3U> reverseMeasured{
                {reverseOpen->network[point].s11, reverseShort->network[point].s11, reverseLoad->network[point].s11}};
            result = VnaCalibration::SolveThreeStandards(reverseMeasured, standards, reverse);
            if (result == VnaCalibration::CalibrationError::NONE) {
                VnaCalibration::TwoPortTerms terms;
                const Complex xf = isolation ? isolation->network[point].s21 : Complex{};
                const Complex xr = isolation ? isolation->network[point].s12 : Complex{};
                result = VnaCalibration::SolveKnownThru(thru->network[point], defined, forward, reverse, xf, xr, terms);
                if (result == VnaCalibration::CalibrationError::NONE) {
                    data.twoPortTerms.push_back(terms);
                }
            }
        } else if (result == VnaCalibration::CalibrationError::NONE) {
            data.onePortTerms.push_back(forward);
        }
        if (result != VnaCalibration::CalibrationError::NONE) {
            error = "Calibration standards are singular, non-finite or invalid at point " + std::to_string(point);
            return false;
        }
    }
    return true;
}

bool CalibrationWorkspace::CanComplete(const std::string &name) const
{
    return collecting_ && !capturePending_ && Detail::CalibrationNameValid(name) &&
           sets_.size() < MAX_CALIBRATION_SETS && nextSetId_ != (std::numeric_limits<std::uint64_t>::max)() &&
           std::all_of(samples_.begin(), samples_.end(), [](const Sample &sample) {
               return sample.step.captured;
           });
}

bool CalibrationWorkspace::Complete(const std::string &name, std::string &error)
{
    if (!CanRevise(error) || !CanComplete(name)) {
        error = "Calibration requires all standards, a valid name and an available set slot";
        return false;
    }
    auto data = *session_.data;
    if (!Solve(data, error)) {
        error_ = error;
        return false;
    }
    CalibrationSet set = session_;
    set.id = nextSetId_++;
    set.name = name.empty() ? (set.name.empty() ? "Cal" + std::to_string(set.id) : set.name) : name;
    set.data = std::make_shared<const VnaCalibration::CalibrationData>(std::move(data));
    auto immutable = std::make_shared<const CalibrationSet>(std::move(set));
    sets_.push_back(immutable);
    selectedSetId_ = immutable->id;
    enabled_ = true;
    collecting_ = false;
    capturePending_ = false;
    samples_.clear();
    session_ = {};
    error_.clear();
    ++revision_;
    return true;
}

bool CalibrationWorkspace::Cancel(std::uint64_t sessionId, std::string &error)
{
    if (!CanRevise(error) || !SessionMatches(sessionId)) {
        error = "Calibration session is missing or changed";
        return false;
    }
    collecting_ = false;
    capturePending_ = false;
    samples_.clear();
    session_ = {};
    error_.clear();
    ++revision_;
    return true;
}

bool CalibrationWorkspace::ApplySaved(
    const CalibrationChange &change, const InstrumentProfile &profile, std::string &error)
{
    if (!CanRevise(error) || collecting_) {
        error = "Saved calibration changes require an idle calibration workspace";
        return false;
    }
    if (!UpdateSaved(change, profile, error)) {
        return false;
    }
    error_.clear();
    ++revision_;
    return true;
}

bool CalibrationWorkspace::UpdateSaved(
    const CalibrationChange &change, const InstrumentProfile &profile, std::string &error)
{
    switch (change.kind) {
        case CalibrationChangeKind::ENABLE:
            if (change.enabled && !Find(selectedSetId_)) {
                error = "No calibration set is selected";
                return false;
            }
            enabled_ = change.enabled;
            return true;
        case CalibrationChangeKind::SELECT:
            if (!Find(change.setId)) {
                error = "Calibration set does not exist";
                return false;
            }
            selectedSetId_ = change.setId;
            return true;
        case CalibrationChangeKind::DELETE_SET:
            return DeleteSet(change.setId, error);
        case CalibrationChangeKind::IMPORT_SET:
            return ImportSet(change.importedSet, profile, error);
        default:
            error = "Unsupported saved calibration operation";
            return false;
    }
}

bool CalibrationWorkspace::DeleteSet(std::uint64_t id, std::string &error)
{
    if (!Find(id)) {
        error = "Calibration set does not exist";
        return false;
    }
    sets_.erase(std::remove_if(sets_.begin(), sets_.end(),
                    [id](const CalibrationSetPtr &set) {
                        return set->id == id;
                    }),
        sets_.end());
    if (selectedSetId_ == id) {
        selectedSetId_ = 0U;
        enabled_ = false;
    }
    return true;
}

bool CalibrationWorkspace::ImportSet(const CalibrationSetPtr &set, const InstrumentProfile &profile, std::string &error)
{
    if (!set || sets_.size() >= MAX_CALIBRATION_SETS || nextSetId_ == (std::numeric_limits<std::uint64_t>::max)() ||
        !Detail::ValidateImportedCalibration(*set, profile, error)) {
        if (error.empty()) {
            error = "Calibration import is missing data or exceeds set capacity";
        }
        return false;
    }
    auto imported = *set;
    imported.id = nextSetId_++;
    if (imported.name.empty()) {
        imported.name = "Cal" + std::to_string(imported.id);
    }
    imported.data = std::make_shared<const VnaCalibration::CalibrationData>(*imported.data);
    sets_.push_back(std::make_shared<const CalibrationSet>(std::move(imported)));
    return true;
}

}  // 命名空间 VnaService
