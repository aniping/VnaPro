/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "vna_core/frame_layout.h"
#include "vna_service/instrument_service.h"

namespace {
using VnaCalibration::Complex;
using Matrix = std::array<Complex, 16U>;
using VnaService::CalibrationChange;
using VnaService::CalibrationChangeKind;
using VnaService::CalibrationStandard;
constexpr unsigned POINTS = 3U;
const std::string OWNER = "TOSM_TEST";

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Near(const Complex &actual, const Complex &expected, const std::string &message)
{
    Require(std::abs(actual - expected) < 1e-10, message);
}

VnaCalibration::FourPortTerms KnownTerms(unsigned point, bool isolation)
{
    VnaCalibration::FourPortTerms terms;
    for (unsigned row = 0U; row < 4U; ++row) {
        for (unsigned column = 0U; column < 4U; ++column) {
            const auto cell = row * 4U + column;
            terms[cell].offset = row == column ? Complex{0.01 * (row + 1U), 0.001 * (point + 1U)}
                                 : isolation   ? Complex{0.0001 * (cell + 1U), -0.00003 * (point + 1U)}
                                               : Complex{};
            terms[cell].match = {0.015 * (row + 1U) - 0.005 * (column + 1U), 0.002 * (point + 1U) * (column + 1U)};
            terms[cell].tracking = {0.7 + 0.015 * (cell + 1U), 0.01 * (point + 1U) * (column + 1U)};
        }
    }
    return terms;
}

Matrix Observe(const Matrix &dut, const VnaCalibration::FourPortTerms &terms)
{
    // 独立前向模型：逐源解 (I - S*diag(match[:,source])) * a = S[:,source]。
    // 生产校正解的是 S=A*inverse(B)，测试不调用任何生产求解或校正函数。
    Matrix measured;
    for (unsigned source = 0U; source < 4U; ++source) {
        std::array<std::array<Complex, 5U>, 4U> system;
        for (unsigned row = 0U; row < 4U; ++row) {
            for (unsigned column = 0U; column < 4U; ++column) {
                system[row][column] = Complex{row == column ? 1.0 : 0.0, 0.0} -
                                      dut[row * 4U + column] * terms[column * 4U + source].match;
            }
            system[row][4U] = dut[row * 4U + source];
        }
        for (unsigned pivot = 0U; pivot < 4U; ++pivot) {
            unsigned best = pivot;
            for (unsigned row = pivot + 1U; row < 4U; ++row) {
                if (std::abs(system[row][pivot]) > std::abs(system[best][pivot])) {
                    best = row;
                }
            }
            std::swap(system[pivot], system[best]);
            const auto divisor = system[pivot][pivot];
            Require(std::abs(divisor) > 1e-8, "independent forward fixture is singular");
            for (unsigned column = pivot; column <= 4U; ++column) {
                system[pivot][column] /= divisor;
            }
            for (unsigned row = 0U; row < 4U; ++row) {
                if (row == pivot) {
                    continue;
                }
                const auto factor = system[row][pivot];
                for (unsigned column = pivot; column <= 4U; ++column) {
                    system[row][column] -= factor * system[pivot][column];
                }
            }
        }
        for (unsigned row = 0U; row < 4U; ++row) {
            const auto cell = row * 4U + source;
            measured[cell] = terms[cell].offset + terms[cell].tracking * system[row][4U];
        }
    }
    return measured;
}

class MatrixDevice final : public VnaCore::Detail::DeviceSession {
public:
    std::string Initialize() override
    {
        return "VnaPro,TosmTest,4PORT,1.0";
    }

    void ApplyPlan(const VnaCore::SweepPlan &) override
    {
    }

    void EnableRf() override
    {
    }

    void Shutdown() override
    {
    }

    void SetMeasured(std::vector<Matrix> measured)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        measured_ = std::move(measured);
    }

    std::vector<VnaCore::PortFrame> AcquireSweep(const VnaCore::SweepPlan &plan) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        std::vector<VnaCore::PortFrame> ports;
        for (const auto source : plan.sourcePorts) {
            VnaCore::PortFrame port;
            port.sourcePort = source;
            port.iq.resize(plan.frequencyHz.size() * VnaCore::IQ_VALUES_PER_POINT, 0.0);
            for (std::size_t point = 0U; point < plan.frequencyHz.size(); ++point) {
                for (unsigned receiver = 1U; receiver <= 4U; ++receiver) {
                    const auto offset =
                        point * VnaCore::IQ_VALUES_PER_POINT + (receiver - 1U) * VnaCore::IQ_VALUES_PER_RECEIVER;
                    const auto value =
                        measured_.empty() ? Complex{} : measured_.at(point)[(receiver - 1U) * 4U + source - 1U];
                    port.iq[offset] = 1.0;
                    port.iq[offset + 2U] = value.real();
                    port.iq[offset + 3U] = value.imag();
                }
            }
            ports.push_back(std::move(port));
        }
        return ports;
    }

private:
    std::mutex mutex_;
    std::vector<Matrix> measured_;
};

struct Fixture final {
    MatrixDevice *device;
    VnaCore::AcquisitionCore core;
    VnaService::InstrumentService service;

    static VnaService::InstrumentProfile Profile(unsigned ports)
    {
        VnaService::InstrumentProfile profile;
        profile.portCount = ports;
        profile.defaultPoints = POINTS;
        return profile;
    }

    explicit Fixture(unsigned ports = 4U) : Fixture(std::make_unique<MatrixDevice>(), ports)
    {
    }

    Fixture(std::unique_ptr<MatrixDevice> owned, unsigned ports)
        : device(owned.get()), core(std::move(owned)), service(core, Profile(ports), false)
    {
        Require(core.Initialize().get().ok, "initialize four-port calibration fixture");
        std::string error;
        Require(service.BootstrapProfile(error) && service.TryAcquireControl(OWNER), error);
    }

    ~Fixture()
    {
        core.Close();
    }
};

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        Require(std::chrono::steady_clock::now() < deadline, "four-port service operation timed out");
        fixture.service.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get();
}

VnaService::OperationResult Submit(Fixture &fixture, CalibrationChange change)
{
    return Finish(
        fixture, fixture.service.ApplyCalibration(std::move(change), fixture.service.Calibration().revision, OWNER));
}

void Begin(Fixture &fixture, bool isolation)
{
    CalibrationChange begin;
    begin.method = VnaCalibration::CalibrationMethod::FOUR_PORT;
    begin.port1 = 1U;
    begin.port2 = 4U;
    begin.includeIsolation = isolation;
    Require(Submit(fixture, begin).ok, "begin four-port TOSM");
    const auto state = fixture.service.Calibration();
    const auto plan = fixture.service.Snapshot().core.configRevision;
    Require(state.steps.size() == (isolation ? 24U : 18U) && state.method == begin.method && plan != 0U,
        "four-port session contains every OSM port and every distinct THRU pair");
}

std::vector<Matrix> StandardValues(const VnaService::CalibrationStep &step, bool isolation)
{
    std::vector<Matrix> measured;
    for (unsigned point = 0U; point < POINTS; ++point) {
        Matrix standard{};
        if (step.kind == CalibrationStandard::OPEN || step.kind == CalibrationStandard::SHORT) {
            standard[(step.port1 - 1U) * 4U + step.port1 - 1U] = step.kind == CalibrationStandard::OPEN ? 1.0 : -1.0;
        } else if (step.kind == CalibrationStandard::THRU) {
            standard[(step.port1 - 1U) * 4U + step.port2 - 1U] = 1.0;
            standard[(step.port2 - 1U) * 4U + step.port1 - 1U] = 1.0;
        }
        measured.push_back(Observe(standard, KnownTerms(point, isolation)));
    }
    return measured;
}

void CaptureAll(Fixture &fixture, bool isolation)
{
    const auto state = fixture.service.Calibration();
    for (const auto &step : state.steps) {
        fixture.device->SetMeasured(StandardValues(step, isolation));
        CalibrationChange capture;
        capture.kind = CalibrationChangeKind::CAPTURE;
        capture.sessionId = state.sessionId;
        capture.stepId = step.id;
        const auto before = fixture.core.ReadView().runId;
        Require(Submit(fixture, capture).ok, "capture " + step.id);
        const auto frame = fixture.core.ReadView().frame;
        Require(frame && frame->runId > before && frame->frameSeq == 1U && frame->ports.size() == 4U &&
                    frame->plan.receiverMask == VnaCore::ALL_RECEIVERS_MASK,
            "every calibration capture is a new complete four-source frame");
    }
}

void FullWorkflow(bool isolation)
{
    Fixture fixture;
    Begin(fixture, isolation);
    CalibrationChange complete;
    complete.kind = CalibrationChangeKind::FINISH;
    complete.sessionId = fixture.service.Calibration().sessionId;
    Require(!Submit(fixture, complete).ok && fixture.service.Calibration().sets.empty(),
        "missing standards cannot publish a partly filled coefficient matrix");
    CaptureAll(fixture, isolation);
    const auto state = fixture.service.Calibration();
    for (const auto &step : state.steps) {
        const auto preview = fixture.service.CalibrationPreview(state.sessionId, step.id);
        const auto expected = StandardValues(step, isolation);
        const auto cell =
            step.port2 == 0U ? (step.port1 - 1U) * 4U + step.port1 - 1U : (step.port2 - 1U) * 4U + step.port1 - 1U;
        Require(preview.available, "every confirmed port/pair remains independently previewable");
        Near({preview.trace.points.front().real, preview.trace.points.front().imag}, expected.front()[cell],
            "same-first-port THRU samples must not overwrite or read another pair");
    }
    const auto recapture = fixture.service.CalibrationPreview(state.sessionId, "thru-1-3");
    auto broken = StandardValues(recapture.step, isolation);
    broken.front()[8U] = {std::nan(""), 0.0};
    fixture.device->SetMeasured(std::move(broken));
    CalibrationChange capture;
    capture.kind = CalibrationChangeKind::CAPTURE;
    capture.sessionId = state.sessionId;
    capture.stepId = recapture.step.id;
    Require(!Submit(fixture, capture).ok, "non-finite THRU recapture must fail");
    const auto retained = fixture.service.CalibrationPreview(state.sessionId, recapture.step.id);
    Require(retained.available && retained.step.runId == recapture.step.runId &&
                retained.trace.points.front().real == recapture.trace.points.front().real,
        "failed recapture cannot relabel an old confirmed standard with the failed frame identity");
    complete.name = "Independent four-port forward model";
    Require(Submit(fixture, complete).ok, "solve and atomically publish all 48 complex error terms per point");
    const auto saved = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    Require(saved && saved->data->fourPortTerms.size() == POINTS && saved->data->onePortTerms.empty() &&
                saved->data->twoPortTerms.empty(),
        "four-port set contains exactly one full matrix representation");
    for (unsigned point = 0U; point < POINTS; ++point) {
        const auto expected = KnownTerms(point, isolation);
        for (unsigned cell = 0U; cell < 16U; ++cell) {
            const auto &actual = saved->data->fourPortTerms[point][cell];
            Near(actual.offset, expected[cell].offset, "offset belongs to the correct physical matrix cell");
            Near(actual.match, expected[cell].match, "source/load match belongs to the correct physical matrix cell");
            Near(actual.tracking, expected[cell].tracking, "non-unit tracking belongs to the correct matrix cell");
        }
    }
    Require(Finish(fixture, fixture.service.DeleteAllMeasurements(OWNER)).ok, "prepare ordered DUT trace catalog");
    std::vector<unsigned> selection;
    for (unsigned receiver = 1U; receiver <= 4U; ++receiver) {
        for (unsigned source = 1U; source <= 4U; ++source) {
            VnaService::Measurement measurement;
            measurement.name = "Dut" + std::to_string(receiver) + std::to_string(source);
            Require(measurement.SetParameter("S" + std::to_string(receiver) + std::to_string(source)) &&
                        Finish(fixture,
                            fixture.service.DefineMeasurement(measurement, fixture.service.Settings().revision, OWNER))
                            .ok,
                "define independent ordered DUT traces");
            selection.push_back(static_cast<unsigned>(selection.size() + 1U));
        }
    }
    std::vector<Matrix> dut(POINTS), measured;
    for (unsigned point = 0U; point < POINTS; ++point) {
        for (unsigned cell = 0U; cell < 16U; ++cell) {
            dut[point][cell] = {0.015 * (cell + 1U) + 0.001 * point, 0.003 * (static_cast<int>(cell % 5U) - 2)};
        }
        measured.push_back(Observe(dut[point], KnownTerms(point, isolation)));
    }
    fixture.device->SetMeasured(measured);
    Require(Finish(fixture, fixture.service.RunSingle(OWNER)).ok, "acquire independent DUT after calibration");
    const auto corrected = fixture.service.MeasurementData(selection, true);
    const auto raw = fixture.service.MeasurementData(selection, false);
    Require(corrected.ok && corrected.values.size() == 16U * POINTS * 2U && raw.ok && raw.values != corrected.values,
        "all 16 outputs must use the real enabled non-unit calibration rather than raw ratios");
    for (unsigned cell = 0U; cell < 16U; ++cell) {
        for (unsigned point = 0U; point < POINTS; ++point) {
            const auto offset = (cell * POINTS + point) * 2U;
            Near({corrected.values[offset], corrected.values[offset + 1U]}, dut[point][cell],
                "joint four-port correction must recover every independent DUT parameter");
        }
    }
}

void ProfileAndImportValidation()
{
    Fixture twoPort(2U);
    CalibrationChange change;
    change.method = VnaCalibration::CalibrationMethod::FOUR_PORT;
    change.port2 = 4U;
    Require(!Submit(twoPort, change).ok && !twoPort.service.Calibration().collecting,
        "two-port profile rejects a four-port calibration before collection");
    Fixture fixture;
    const auto axis = fixture.service.StimulusAxis().values;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = VnaCalibration::CalibrationMethod::FOUR_PORT;
    data->port1 = 1U;
    data->port2 = 4U;
    data->frequencyHz = axis;
    for (unsigned point = 0U; point < POINTS; ++point) {
        data->fourPortTerms.push_back(KnownTerms(point, false));
    }
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->name = "Validated full matrix";
    set->deviceIdentity = fixture.service.Identity();
    set->ifBandwidthHz = fixture.service.Settings().ifBandwidthHz;
    set->powerDbm.assign(POINTS, fixture.service.Settings().powerDbm);
    set->data = data;
    change.kind = CalibrationChangeKind::IMPORT_SET;
    change.importedSet = set;
    Require(Submit(fixture, change).ok, "import valid four-port coefficients through ordinary service validation");
    const auto revision = fixture.service.Calibration().revision;
    auto malformed = std::make_shared<VnaService::CalibrationSet>(*set);
    auto invalid = std::make_shared<VnaCalibration::CalibrationData>(*data);
    invalid->fourPortTerms[1U][7U].tracking = {};
    malformed->data = invalid;
    change.importedSet = malformed;
    Require(!Submit(fixture, change).ok && fixture.service.Calibration().revision == revision &&
                fixture.service.Calibration().sets.size() == 1U,
        "a single missing tracking cell rejects the whole imported matrix without mutation");
    change.importedSet = set;
    Require(!Submit(twoPort, change).ok && twoPort.service.Calibration().sets.empty(),
        "four-port imported sets cannot bypass profile capability checks");
}

void FailedFourPortSolveKeepsPreviousSet()
{
    Fixture fixture;
    Begin(fixture, false);
    CaptureAll(fixture, false);
    CalibrationChange finish;
    finish.kind = CalibrationChangeKind::FINISH;
    finish.sessionId = fixture.service.Calibration().sessionId;
    Require(Submit(fixture, finish).ok, "prepare a valid prior four-port set");
    const auto prior = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    Begin(fixture, false);
    const auto state = fixture.service.Calibration();
    for (auto step : state.steps) {
        const auto id = step.id;
        if (step.port1 == 3U && step.port2 == 0U) {
            step.kind = CalibrationStandard::LOAD;
        }
        fixture.device->SetMeasured(StandardValues(step, false));
        CalibrationChange capture;
        capture.kind = CalibrationChangeKind::CAPTURE;
        capture.sessionId = state.sessionId;
        capture.stepId = id;
        Require(Submit(fixture, capture).ok, "capture finite but degenerate port-three standards");
    }
    finish.sessionId = state.sessionId;
    Require(!Submit(fixture, finish).ok && fixture.service.Calibration().collecting &&
                fixture.service.Calibration().selectedSetId == prior->id &&
                fixture.service.FindCalibrationSet(prior->id) == prior &&
                fixture.service.Calibration().sets.size() == 1U,
        "singular reflection solve must keep the previous full set and retryable session");
}

void Run(const char *name, void (*test)(), unsigned &failed)
{
    try {
        test();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}
}  // 匿名命名空间

int main()
{
    unsigned failed = 0U;
    Run(
        "18 standards, 48 complex error terms, independent 16S DUT",
        [] {
            FullWorkflow(false);
        },
        failed);
    Run(
        "24 standards with directional isolation",
        [] {
            FullWorkflow(true);
        },
        failed);
    Run("four-port profile and import validation", ProfileAndImportValidation, failed);
    Run("failed four-port solve preserves completed coefficients", FailedFourPortSolveKeepsPreviousSet, failed);
    return failed == 0U ? 0 : 1;
}
