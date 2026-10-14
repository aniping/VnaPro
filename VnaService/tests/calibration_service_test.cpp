/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "vna_core/frame_layout.h"
#include "vna_service/instrument_service.h"

namespace {
using Complex = std::complex<double>;
using VnaCalibration::TwoPortNetwork;
using VnaService::CalibrationChange;
using VnaService::CalibrationChangeKind;
using VnaService::CalibrationStandard;
constexpr unsigned FIRST_PORT = 2U;
constexpr unsigned SECOND_PORT = 4U;
constexpr std::chrono::seconds TIMEOUT(3);

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Near(const Complex &actual, const Complex &expected, const std::string &message)
{
    Require(std::abs(actual - expected) < 1e-11, message);
}

class StandardDevice final : public VnaCore::Detail::DeviceSession {
public:
    std::string Initialize() override
    {
        return "VnaPro,CalibrationTest,4PORT,1.0";
    }

    void ApplyPlan(const VnaCore::SweepPlan &) override
    {
    }

    void EnableRf() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++enableCount_;
        enableEntered_ = true;
        wake_.wait(lock, [this] {
            return !enableBlocked_;
        });
        if (failEnable_) {
            throw std::runtime_error("calibration acquisition restart failed");
        }
    }

    void Shutdown() override
    {
    }

    void SetNetwork(TwoPortNetwork value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = value;
    }

    void Arm(bool fail = false)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        blocked_ = true;
        entered_ = false;
        fail_ = fail;
    }

    bool Entered() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entered_;
    }

    void Release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        blocked_ = false;
        wake_.notify_all();
    }

    void ArmEnableFailure()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enableBlocked_ = true;
        enableEntered_ = false;
        failEnable_ = true;
    }

    bool EnableEntered() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return enableEntered_;
    }

    unsigned EnableCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return enableCount_;
    }

    void ReleaseEnable()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enableBlocked_ = false;
        wake_.notify_all();
    }

    std::vector<VnaCore::PortFrame> AcquireSweep(const VnaCore::SweepPlan &plan) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        wake_.wait(lock, [this] {
            return !blocked_;
        });
        if (fail_) {
            throw std::runtime_error("standard acquisition failed");
        }
        const auto measured = value_;
        lock.unlock();
        std::vector<VnaCore::PortFrame> result;
        for (auto source : plan.sourcePorts) {
            VnaCore::PortFrame port;
            port.sourcePort = source;
            port.iq.resize(plan.frequencyHz.size() * VnaCore::IQ_VALUES_PER_POINT, 0.0);
            for (std::size_t point = 0U; point < plan.frequencyHz.size(); ++point) {
                for (unsigned receiver = 1U; receiver <= 4U; ++receiver) {
                    const auto base =
                        point * VnaCore::IQ_VALUES_PER_POINT + (receiver - 1U) * VnaCore::IQ_VALUES_PER_RECEIVER;
                    port.iq[base] = 1.0;
                    Complex value{0.23, -0.05};
                    if (source == FIRST_PORT && receiver == FIRST_PORT) {
                        value = measured.s11;
                    }
                    if (source == FIRST_PORT && receiver == SECOND_PORT) {
                        value = measured.s21;
                    }
                    if (source == SECOND_PORT && receiver == SECOND_PORT) {
                        value = measured.s22;
                    }
                    if (source == SECOND_PORT && receiver == FIRST_PORT) {
                        value = measured.s12;
                    }
                    port.iq[base + 2U] = value.real();
                    port.iq[base + 3U] = value.imag();
                }
            }
            result.push_back(std::move(port));
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    TwoPortNetwork value_;
    bool blocked_ = false;
    bool entered_ = false;
    bool fail_ = false;
    bool enableBlocked_ = false;
    bool enableEntered_ = false;
    bool failEnable_ = false;
    unsigned enableCount_ = 0U;
};

VnaService::InstrumentProfile Profile()
{
    VnaService::InstrumentProfile profile;
    profile.defaultPoints = 3U;
    return profile;
}

struct Fixture final {
    StandardDevice *device;
    VnaCore::AcquisitionCore core;
    VnaService::InstrumentService service;

    Fixture() : Fixture(std::make_unique<StandardDevice>())
    {
    }

    explicit Fixture(std::unique_ptr<StandardDevice> owned)
        : device(owned.get()), core(std::move(owned)), service(core, Profile(), false)
    {
        Require(core.Initialize().get().ok, "initialize test device");
        std::string error;
        Require(service.BootstrapProfile(error), error);
        Require(service.TryAcquireControl("TEST"), "claim test control");
    }

    ~Fixture()
    {
        device->Release();
        device->ReleaseEnable();
        core.Close();
    }
};

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "service calibration operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get();
}

VnaService::Operation Submit(Fixture &fixture, CalibrationChange change)
{
    return fixture.service.ApplyCalibration(std::move(change), fixture.service.Calibration().revision, "TEST");
}

void Begin(Fixture &fixture, bool twoPort = false, bool isolation = false)
{
    CalibrationChange change;
    change.kind = CalibrationChangeKind::BEGIN;
    change.method = twoPort ? VnaCalibration::CalibrationMethod::TWO_PORT : VnaCalibration::CalibrationMethod::ONE_PORT;
    change.port1 = FIRST_PORT;
    change.port2 = twoPort ? SECOND_PORT : 0U;
    change.includeIsolation = isolation;
    Require(Finish(fixture, Submit(fixture, change)).ok, "begin calibration");
}

CalibrationChange CaptureChange(Fixture &fixture, CalibrationStandard kind, unsigned port)
{
    const auto state = fixture.service.Calibration();
    for (const auto &step : state.steps) {
        if (step.kind == kind && step.port1 == port) {
            CalibrationChange change;
            change.kind = CalibrationChangeKind::CAPTURE;
            change.sessionId = state.sessionId;
            change.stepId = step.id;
            return change;
        }
    }
    throw std::runtime_error("calibration step not found");
}

void Capture(Fixture &fixture, CalibrationStandard kind, unsigned port, const TwoPortNetwork &measured)
{
    fixture.device->SetNetwork(measured);
    const auto oldRun = fixture.core.ReadView().runId;
    Require(Finish(fixture, Submit(fixture, CaptureChange(fixture, kind, port))).ok, "capture new standard");
    const auto view = fixture.core.ReadView();
    Require(view.state == VnaCore::CoreState::READY && view.runId > oldRun && view.frame &&
                view.frame->runId == view.runId && view.frame->frameSeq == 1U,
        "capture uses a fresh stopped run");
}

Complex Reflection(const Complex &gamma, bool reverse = false)
{
    // 独立双线性前向模型，不调用生产求解或校正函数。
    const Complex d = reverse ? Complex{-0.015, 0.008} : Complex{0.02, 0.01};
    const Complex s = reverse ? Complex{0.07, 0.02} : Complex{-0.10, 0.03};
    const Complex r = reverse ? Complex{0.95, 0.04} : Complex{0.92, -0.08};
    return ((r - d * s) * gamma + d) / (Complex{1.0, 0.0} - s * gamma);
}

TwoPortNetwork Observe(const TwoPortNetwork &network)
{
    const Complex df{0.02, 0.01}, sf{-0.10, 0.03}, rf{0.92, -0.08};
    const Complex dr{-0.015, 0.008}, sr{0.07, 0.02}, rr{0.95, 0.04};
    const Complex tf{0.8, 0.12}, lf{0.08, -0.02}, xf{0.001, 0.0005};
    const Complex tr{0.85, -0.09}, lr{-0.05, 0.025}, xr{-0.0008, 0.0003};
    const Complex product = network.s21 * network.s12;
    const Complex hf = (1.0 - sf * network.s11) * (1.0 - lf * network.s22) - sf * lf * product;
    const Complex hr = (1.0 - sr * network.s22) * (1.0 - lr * network.s11) - sr * lr * product;
    TwoPortNetwork measured;
    measured.s11 = df + rf * (network.s11 * (1.0 - lf * network.s22) + lf * product) / hf;
    measured.s21 = xf + tf * network.s21 / hf;
    measured.s22 = dr + rr * (network.s22 * (1.0 - lr * network.s11) + lr * product) / hr;
    measured.s12 = xr + tr * network.s12 / hr;
    return measured;
}

void CaptureOsl(Fixture &fixture, unsigned port)
{
    const std::array<Complex, 3U> gamma{{Complex{1.0, 0.0}, Complex{-1.0, 0.0}, Complex{}}};
    const std::array<CalibrationStandard, 3U> standards{
        {CalibrationStandard::OPEN, CalibrationStandard::SHORT, CalibrationStandard::LOAD}};
    for (std::size_t index = 0U; index < standards.size(); ++index) {
        TwoPortNetwork measured;
        if (port == FIRST_PORT) {
            measured.s11 = Reflection(gamma[index]);
        } else {
            measured.s22 = Reflection(gamma[index], true);
        }
        Capture(fixture, standards[index], port, measured);
    }
}

void Complete(Fixture &fixture)
{
    CalibrationChange change;
    change.kind = CalibrationChangeKind::FINISH;
    change.sessionId = fixture.service.Calibration().sessionId;
    change.name = "Independent fixture";
    Require(Finish(fixture, Submit(fixture, change)).ok, "solve calibration and publish set");
}

void SelectMeasurement(Fixture &fixture, unsigned receiver, unsigned source)
{
    VnaService::Measurement measurement;
    measurement.name = "Trc1";
    measurement.kind = VnaService::MeasurementKind::S_PARAMETER;
    measurement.receiverKind = 'b';
    measurement.receiverPort = receiver;
    measurement.sourcePort = source;
    Require(
        Finish(fixture, fixture.service.UpdateMeasurement(measurement, fixture.service.Settings().revision, "TEST")).ok,
        "select DUT parameter");
}

void OnePortWorkflowAndRawRead()
{
    Fixture fixture;
    Require(Finish(fixture, fixture.service.RunSingle("TEST")).ok, "publish an old DUT frame");
    const auto old = fixture.core.ReadView().frame;
    const auto revision = fixture.service.Settings().revision;
    Begin(fixture);
    Require(fixture.service.Settings().revision == revision && fixture.service.Settings().measurements.size() == 1U,
        "calibration does not alter measurement definitions");
    CaptureOsl(fixture, FIRST_PORT);
    Require(old->runId != fixture.core.ReadView().frame->runId, "old DUT frame never becomes a standard");
    Complete(fixture);
    const auto state = fixture.service.Calibration();
    Require(state.enabled && state.compatible && !state.collecting && state.sets.size() == 1U,
        "completed calibration enables compatible correction");
    SelectMeasurement(fixture, FIRST_PORT, FIRST_PORT);
    TwoPortNetwork measured;
    measured.s11 = Reflection(Complex{0.3, -0.4});
    fixture.device->SetNetwork(measured);
    Require(Finish(fixture, fixture.service.RunSingle("TEST")).ok, "acquire independent DUT");
    const auto corrected = fixture.service.MeasurementData();
    const auto raw = fixture.service.MeasurementData(false);
    Require(corrected.ok && raw.ok, "both corrected and raw data are available");
    Near({corrected.values[0U], corrected.values[1U]}, {0.3, -0.4}, "one-port corrected independent DUT");
    Near({raw.values[0U], raw.values[1U]}, measured.s11, "raw normalized data remains unchanged");
    CalibrationChange off;
    off.kind = CalibrationChangeKind::ENABLE;
    off.enabled = false;
    Require(Finish(fixture, Submit(fixture, off)).ok, "disable correction");
    const auto disabled = fixture.service.MeasurementData();
    Near({disabled.values[0U], disabled.values[1U]}, measured.s11, "disabled correction reads raw");
}

void TwoPortArbitraryPairAndSourceUnion()
{
    Fixture fixture;
    Begin(fixture, true, true);
    CaptureOsl(fixture, FIRST_PORT);
    CaptureOsl(fixture, SECOND_PORT);
    TwoPortNetwork thru;
    thru.s21 = thru.s12 = Complex{1.0, 0.0};
    Capture(fixture, CalibrationStandard::THRU, FIRST_PORT, Observe(thru));
    TwoPortNetwork isolation;
    Capture(fixture, CalibrationStandard::ISOLATION, FIRST_PORT, Observe(isolation));
    Complete(fixture);
    SelectMeasurement(fixture, SECOND_PORT, FIRST_PORT);
    TwoPortNetwork truth;
    truth.s11 = {0.2, 0.1};
    truth.s21 = {0.6, -0.2};
    truth.s22 = {-0.12, 0.15};
    truth.s12 = {0.55, -0.1};
    fixture.device->SetNetwork(Observe(truth));
    Require(Finish(fixture, fixture.service.RunSingle("TEST")).ok, "acquire full coupled DUT");
    const auto frame = fixture.core.ReadView().frame;
    Require(std::find(frame->plan.sourcePorts.begin(), frame->plan.sourcePorts.end(), SECOND_PORT) !=
                frame->plan.sourcePorts.end(),
        "S42 correction retains reverse source four");
    Require(fixture.service.Settings().measurements.size() == 1U,
        "source union does not create hidden measurement definitions");
    const auto result = fixture.service.MeasurementData();
    Require(result.ok, result.error);
    Near({result.values[0U], result.values[1U]}, truth.s21, "arbitrary pair twelve-term correction");
    const auto snapshot = fixture.service.Snapshot();
    Require(
        snapshot.correction && snapshot.correction->port1 == FIRST_PORT && snapshot.correction->port2 == SECOND_PORT,
        "snapshot carries immutable same-frame correction context");
    Near(snapshot.correction->twoPortTerms[0U].forwardTransmission.isolation, {0.001, 0.0005},
        "measured isolation retained");
}

void SessionLocksAndCancelBarrier()
{
    Fixture fixture;
    CalibrationChange begin;
    begin.kind = CalibrationChangeKind::BEGIN;
    begin.port1 = FIRST_PORT;
    const auto beginOperation = Submit(fixture, begin);
    VnaService::ConfigPatch patch;
    patch.hasPoints = true;
    patch.points = 5U;
    const auto queuedConfig = fixture.service.ApplyConfiguration(patch, fixture.service.Settings().revision, "TEST");
    const auto queuedRun = fixture.service.RunContinuous("TEST");
    Require(Finish(fixture, beginOperation).ok, "begin precedes already admitted configuration");
    Require(!Finish(fixture, queuedConfig).ok && !Finish(fixture, queuedRun).ok,
        "execution guards reject queued config and RUN during collection");
    fixture.device->Arm();
    const auto capture = Submit(fixture, CaptureChange(fixture, CalibrationStandard::OPEN, FIRST_PORT));
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (!fixture.device->Entered()) {
        fixture.service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "capture enters controlled scan");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(!fixture.service.MeasurementData().ok && fixture.service.Calibration().capturePending,
        "pending standard cannot expose retained data as current");
    CalibrationChange cancel;
    cancel.kind = CalibrationChangeKind::CANCEL;
    cancel.sessionId = fixture.service.Calibration().sessionId;
    const auto cancelOperation = Submit(fixture, cancel);
    Require(cancelOperation.wait_for(std::chrono::seconds(0)) != std::future_status::ready,
        "cancel waits for accepted capture stop barrier");
    fixture.device->Release();
    Require(Finish(fixture, capture).ok && Finish(fixture, cancelOperation).ok,
        "same-session cancel tolerates revision changed by its prior capture");
    Require(!fixture.service.Calibration().collecting &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD,
        "cancel clears session and ends in HOLD");
    Require(!Finish(fixture, Submit(fixture, cancel)).ok, "old session cancellation cannot affect a new session");
}

void SingularSessionPreservesCompletedSet()
{
    Fixture fixture;
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    Complete(fixture);
    auto saved = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    Complete(fixture);
    saved = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    Near(saved->data->onePortTerms[0U].directivity, {0.02, 0.01},
        "recapture standard bypasses the previously enabled calibration");
    Begin(fixture);
    const TwoPortNetwork fixedDut{};
    Capture(fixture, CalibrationStandard::OPEN, FIRST_PORT, fixedDut);
    Capture(fixture, CalibrationStandard::SHORT, FIRST_PORT, fixedDut);
    Capture(fixture, CalibrationStandard::LOAD, FIRST_PORT, fixedDut);
    CalibrationChange finish;
    finish.kind = CalibrationChangeKind::FINISH;
    finish.sessionId = fixture.service.Calibration().sessionId;
    Require(!Finish(fixture, Submit(fixture, finish)).ok, "unchanged simulated DUT cannot solve OSL");
    Require(fixture.service.FindCalibrationSet(saved->id) == saved &&
                fixture.service.Calibration().selectedSetId == saved->id &&
                fixture.service.Calibration().sets.size() == 2U,
        "failed solve keeps previous immutable active set");
}

void SetPoolImportAndStimulusCompatibility()
{
    Fixture fixture;
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    Complete(fixture);
    const auto saved = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    VnaService::ConfigPatch patch;
    patch.hasIfBandwidthHz = true;
    patch.ifBandwidthHz = 10000.0;
    Require(Finish(fixture, fixture.service.ApplyConfiguration(patch, fixture.service.Settings().revision, "TEST")).ok,
        "change physical stimulus");
    Require(!fixture.service.Calibration().compatible && !fixture.service.Snapshot().correction,
        "changed IF bandwidth cannot use old coefficients");
    const auto summary = fixture.service.Calibration().sets.front();
    Require(summary.startHz == saved->data->frequencyHz.front() && summary.stopHz == saved->data->frequencyHz.back() &&
                summary.points == saved->data->frequencyHz.size() && summary.ifBandwidthHz == saved->ifBandwidthHz &&
                summary.ifBandwidthHz != patch.ifBandwidthHz,
        "set summary retains its own stimulus instead of reporting the current incompatible configuration");
    patch.ifBandwidthHz = saved->ifBandwidthHz;
    Require(Finish(fixture, fixture.service.ApplyConfiguration(patch, fixture.service.Settings().revision, "TEST")).ok,
        "restore physical stimulus");
    Require(fixture.service.Calibration().compatible && fixture.service.Snapshot().correction,
        "same stimulus restores compatibility despite different acquisition revision");
    CalibrationChange import;
    import.kind = CalibrationChangeKind::IMPORT_SET;
    import.importedSet = saved;
    for (std::size_t count = 1U; count < VnaService::MAX_CALIBRATION_SETS; ++count) {
        Require(Finish(fixture, Submit(fixture, import)).ok, "import validated calibration into bounded pool");
    }
    Require(!Finish(fixture, Submit(fixture, import)).ok, "full pool rejects import without mutation");
    Require(
        fixture.service.Calibration().sets.size() == VnaService::MAX_CALIBRATION_SETS, "pool capacity remains bounded");
    CalibrationChange remove;
    remove.kind = CalibrationChangeKind::DELETE_SET;
    remove.setId = saved->id;
    Require(Finish(fixture, Submit(fixture, remove)).ok, "delete selected set");
    Require(!fixture.service.Calibration().enabled && !fixture.service.Snapshot().correction,
        "delete selected set disables correction");
    auto corrupted = std::make_shared<VnaService::CalibrationSet>(*saved);
    auto badData = std::make_shared<VnaCalibration::CalibrationData>(*saved->data);
    badData->onePortTerms[0U].directivity = {std::nan(""), 0.0};
    corrupted->data = badData;
    import.importedSet = corrupted;
    Require(!Finish(fixture, Submit(fixture, import)).ok, "import rejects non-finite coefficients at service boundary");
    badData->onePortTerms[0U].directivity = {0.02, 0.01};
    badData->onePortTerms[0U].reflectionTracking = {1e-100, 0.0};
    Require(
        !Finish(fixture, Submit(fixture, import)).ok, "import rejects tracking below the mathematics validity limit");
}

void FailedAcquisitionCannotCaptureOldFrame()
{
    Fixture fixture;
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    const auto old = fixture.core.ReadView().frame;
    const auto step = fixture.service.Calibration().steps.front();
    fixture.device->Arm(true);
    const auto capture = Submit(fixture, CaptureChange(fixture, CalibrationStandard::OPEN, FIRST_PORT));
    fixture.device->Release();
    Require(!Finish(fixture, capture).ok, "failed new scan is rejected");
    const auto state = fixture.service.Calibration();
    Require(!state.capturePending && state.steps.front().runId == step.runId &&
                state.steps.front().frameSeq == step.frameSeq && fixture.core.ReadView().frame == old,
        "failed recapture retains original sample without relabelling old frame");
    Require(!fixture.service.MeasurementData().ok && !fixture.service.Snapshot().correction,
        "faulted core cannot expose corrected data");
    const auto preview = fixture.service.CalibrationPreview(state.sessionId, step.id);
    Require(preview.available && preview.step.runId == step.runId && preview.step.frameSeq == step.frameSeq,
        "fault preserves the previously confirmed raw standard preview without claiming a new frame");
    CalibrationChange cancel;
    cancel.kind = CalibrationChangeKind::CANCEL;
    cancel.sessionId = state.sessionId;
    Require(Finish(fixture, Submit(fixture, cancel)).ok && !fixture.service.Calibration().collecting,
        "failed hardware does not prevent cancelling its calibration session");
}

void OldSessionFailureCannotAlterCurrentSession()
{
    Fixture fixture;
    Begin(fixture);
    const auto previous = fixture.service.Calibration();
    CalibrationChange cancel;
    cancel.kind = CalibrationChangeKind::CANCEL;
    cancel.sessionId = previous.sessionId;
    Require(Finish(fixture, Submit(fixture, cancel)).ok, "cancel previous session");
    Begin(fixture);
    const auto current = fixture.service.Calibration();
    for (auto kind : {CalibrationChangeKind::CAPTURE, CalibrationChangeKind::FINISH}) {
        CalibrationChange stale;
        stale.kind = kind;
        stale.sessionId = previous.sessionId;
        stale.stepId = previous.steps.front().id;
        Require(!Finish(fixture, Submit(fixture, stale)).ok, "reject old session operation");
        const auto result = fixture.service.Calibration();
        Require(result.revision == current.revision && result.sessionId == current.sessionId && result.error.empty() &&
                    !result.capturePending && !result.steps.front().captured,
            "a rejected foreign session request cannot mutate current calibration state or its error");
        stale.sessionId = current.sessionId;
        const auto staleCas = fixture.service.ApplyCalibration(stale, current.revision - 1U, "TEST");
        Require(!Finish(fixture, staleCas).ok, "reject stale CAS even for the same session");
        const auto afterCas = fixture.service.Calibration();
        Require(afterCas.revision == current.revision && afterCas.sessionId == current.sessionId &&
                    afterCas.error.empty() && !afterCas.capturePending && !afterCas.steps.front().captured,
            "same-session stale CAS cannot mutate calibration status or its error");
    }
    auto unknown = CaptureChange(fixture, CalibrationStandard::OPEN, FIRST_PORT);
    unknown.stepId = "unknown-standard";
    Require(!Finish(fixture, Submit(fixture, unknown)).ok, "reject unknown standard");
    Require(fixture.service.Calibration().error.empty() && fixture.service.Calibration().revision == current.revision,
        "unknown standard rejection does not become a current-session acquisition error");
}

void AcceptedImportFreezesCallerAliases()
{
    Fixture fixture;
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    Complete(fixture);
    const auto saved = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    auto callerData = std::make_shared<VnaCalibration::CalibrationData>(*saved->data);
    auto callerSet = std::make_shared<VnaService::CalibrationSet>(*saved);
    callerSet->data = callerData;
    callerSet->name = "Accepted snapshot";
    fixture.device->Arm();
    const auto scan = fixture.service.RunSingle("TEST");
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (!fixture.device->Entered()) {
        fixture.service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "preceding scan reaches its blocked acquisition");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CalibrationChange change;
    change.kind = CalibrationChangeKind::IMPORT_SET;
    change.importedSet = callerSet;
    const auto imported = Submit(fixture, change);
    callerData->onePortTerms[0U].directivity = {std::nan(""), 0.0};
    callerSet->name = "Changed after admission";
    fixture.device->Release();
    Require(Finish(fixture, scan).ok && Finish(fixture, imported).ok,
        "queued import uses the accepted immutable payload instead of caller aliases");
    const auto newSet = fixture.service.FindCalibrationSet(fixture.service.Calibration().sets.back().id);
    Require(newSet && newSet->name == "Accepted snapshot", "import name is frozen before FIFO execution");
    Near(newSet->data->onePortTerms[0U].directivity, saved->data->onePortTerms[0U].directivity,
        "import coefficients are frozen before FIFO execution");
}

void StandardPreviewIsRawReadOnlyAndSessionScoped()
{
    Fixture fixture;
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    Complete(fixture);
    const auto oldSet = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    Begin(fixture, true, true);
    const auto pending = fixture.service.Calibration();
    const auto open = CaptureChange(fixture, CalibrationStandard::OPEN, FIRST_PORT);
    Require(!fixture.service.CalibrationPreview(pending.sessionId, open.stepId).available,
        "unmeasured standard preview cannot fabricate a zero curve");
    const auto missingPreview = fixture.service.CalibrationPreview(pending.sessionId, "unknown-standard");
    Require(!missingPreview.available && missingPreview.sessionId == pending.sessionId &&
                missingPreview.step.id == "unknown-standard" &&
                !fixture.service.CalibrationPreview(pending.sessionId - 1U, open.stepId).available,
        "preview rejects unknown steps and stale sessions");
    TwoPortNetwork measured;
    measured.s11 = Reflection({1.0, 0.0});
    Capture(fixture, CalibrationStandard::OPEN, FIRST_PORT, measured);
    TwoPortNetwork thru;
    thru.s21 = thru.s12 = {1.0, 0.0};
    Capture(fixture, CalibrationStandard::THRU, FIRST_PORT, Observe(thru));
    const auto state = fixture.service.Calibration();
    const auto frame = fixture.core.ReadView().frame;
    const auto settingsRevision = fixture.service.Settings().revision;
    const auto acquisitionRevision = fixture.service.Settings().acquisitionRevision;
    const auto preview = fixture.service.CalibrationPreview(state.sessionId, open.stepId);
    Require(preview.available && preview.trace.available && !preview.trace.calibrated &&
                preview.measurement.Parameter() == "S22" && preview.trace.points.size() == preview.frequencyHz.size() &&
                preview.calibrationRevision == state.revision && preview.sessionId == state.sessionId &&
                preview.ifBandwidthHz == oldSet->ifBandwidthHz,
        "reflection preview provides confirmed raw Spp and frozen stimulus");
    Near({preview.trace.points.front().real, preview.trace.points.front().imag}, measured.s11,
        "preview never applies the previous enabled calibration");
    const auto pair = CaptureChange(fixture, CalibrationStandard::THRU, FIRST_PORT);
    const auto transmission = fixture.service.CalibrationPreview(state.sessionId, pair.stepId);
    Require(transmission.available && transmission.measurement.Parameter() == "S42" &&
                transmission.step.runId == frame->runId && transmission.step.frameSeq == frame->frameSeq,
        "THRU preview uses the captured forward transmission and its own source identity");
    Near({transmission.trace.points.front().real, transmission.trace.points.front().imag}, Observe(thru).s21,
        "THRU preview returns stored raw forward value");
    Require(fixture.service.Calibration().revision == state.revision &&
                fixture.service.Settings().revision == settingsRevision &&
                fixture.service.Settings().acquisitionRevision == acquisitionRevision &&
                fixture.service.Settings().measurements.size() == 1U && fixture.core.ReadView().frame == frame &&
                fixture.service.FindCalibrationSet(oldSet->id) == oldSet,
        "preview leaves calibration state, versions, measurement catalog, frame and old coefficients unchanged");
}

void VerifyRestoredEntry(Fixture &fixture, VnaService::TriggerSource source, VnaService::SweepMode mode,
    const VnaCore::FramePtr &standardFrame)
{
    const auto settings = fixture.service.Settings();
    Require(settings.triggerSource == source && settings.sweepMode == mode,
        "calibration did not restore entry trigger/sweep mode: source=" +
            std::to_string(static_cast<int>(settings.triggerSource)) +
            ", sweep=" + std::to_string(static_cast<int>(settings.sweepMode)));
    const auto view = fixture.core.ReadView();
    if (mode == VnaService::SweepMode::HOLD) {
        Require(view.state == VnaCore::CoreState::READY && view.frame == standardFrame,
            "entry HOLD must not start a new acquisition run");
        return;
    }
    Require(view.state == VnaCore::CoreState::RUNNING && view.runId > standardFrame->runId,
        "restored non-HOLD mode must start a new core run before the operation reports success");
    if (source == VnaService::TriggerSource::MANUAL) {
        Require(view.frame == standardFrame && !fixture.service.MeasurementData().ok,
            "restored MANUAL must be armed without relabelling the standard as current DUT data");
        Require(Finish(fixture, fixture.service.Trigger("TEST")).ok, "restored manual run accepts INIT");
    } else {
        const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
        while (!fixture.core.ReadView().frame || fixture.core.ReadView().frame->runId != view.runId) {
            fixture.service.Poll();
            Require(std::chrono::steady_clock::now() < deadline, "restored CONT did not publish a fresh DUT frame");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void FinishRestoresEntryModes(VnaService::TriggerSource source, VnaService::SweepMode mode)
{
    Fixture fixture;
    Require(Finish(fixture, fixture.service.SetTriggerSource(source, "TEST")).ok, "prepare entry trigger source");
    Require(Finish(fixture, fixture.service.SetSweepMode(mode, "TEST")).ok, "prepare entry sweep mode");
    Begin(fixture);
    Require(fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                fixture.service.Settings().triggerSource == VnaService::TriggerSource::MANUAL &&
                fixture.core.ReadView().state == VnaCore::CoreState::READY,
        "calibration holds ordinary acquisition and uses manual standard triggers");
    CaptureOsl(fixture, FIRST_PORT);
    const auto standardFrame = fixture.core.ReadView().frame;
    TwoPortNetwork dut;
    dut.s11 = Reflection({0.3, -0.4});
    fixture.device->SetNetwork(dut);
    Complete(fixture);
    VerifyRestoredEntry(fixture, source, mode, standardFrame);
}

void FinishRestoresContinuous()
{
    FinishRestoresEntryModes(VnaService::TriggerSource::IMMEDIATE, VnaService::SweepMode::CONTINUOUS);
}

void FinishRestoresManualContinuous()
{
    FinishRestoresEntryModes(VnaService::TriggerSource::MANUAL, VnaService::SweepMode::CONTINUOUS);
}

void FinishRestoresManualSingle()
{
    FinishRestoresEntryModes(VnaService::TriggerSource::MANUAL, VnaService::SweepMode::SINGLE);
}

void FinishPreservesManualHold()
{
    FinishRestoresEntryModes(VnaService::TriggerSource::MANUAL, VnaService::SweepMode::HOLD);
}

void CancelRestoresEntryModes()
{
    using Source = VnaService::TriggerSource;
    using Mode = VnaService::SweepMode;
    const std::array<std::pair<Source, Mode>, 4U> modes{{{Source::IMMEDIATE, Mode::CONTINUOUS},
        {Source::MANUAL, Mode::CONTINUOUS}, {Source::MANUAL, Mode::SINGLE}, {Source::MANUAL, Mode::HOLD}}};
    for (const auto &entry : modes) {
        Fixture fixture;
        Require(Finish(fixture, fixture.service.SetTriggerSource(entry.first, "TEST")).ok, "prepare cancel source");
        Require(Finish(fixture, fixture.service.SetSweepMode(entry.second, "TEST")).ok, "prepare cancel mode");
        Begin(fixture);
        CalibrationChange duplicate;
        duplicate.kind = CalibrationChangeKind::BEGIN;
        duplicate.port1 = FIRST_PORT;
        Require(!Finish(fixture, Submit(fixture, duplicate)).ok, "rejected BEGIN cannot replace entry mode snapshot");
        TwoPortNetwork open;
        open.s11 = Reflection({1.0, 0.0});
        Capture(fixture, CalibrationStandard::OPEN, FIRST_PORT, open);
        const auto standardFrame = fixture.core.ReadView().frame;
        CalibrationChange cancel;
        cancel.kind = CalibrationChangeKind::CANCEL;
        cancel.sessionId = fixture.service.Calibration().sessionId;
        Require(Finish(fixture, Submit(fixture, cancel)).ok, "normal cancellation restores entry state");
        Require(!fixture.service.Calibration().collecting, "cancellation closes its own session");
        VerifyRestoredEntry(fixture, entry.first, entry.second, standardFrame);
    }
}

void FailedSolveKeepsEntryModeForRetry()
{
    Fixture fixture;
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    Complete(fixture);
    const auto previous = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    Require(Finish(fixture, fixture.service.RunContinuous("TEST")).ok, "enter CONT before failed solve");
    Begin(fixture);
    for (auto kind : {CalibrationStandard::OPEN, CalibrationStandard::SHORT, CalibrationStandard::LOAD}) {
        Capture(fixture, kind, FIRST_PORT, {});
    }
    CalibrationChange finish;
    finish.kind = CalibrationChangeKind::FINISH;
    finish.sessionId = fixture.service.Calibration().sessionId;
    Require(!Finish(fixture, Submit(fixture, finish)).ok, "singular solve fails before recovery");
    Require(fixture.service.Calibration().collecting &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                fixture.service.FindCalibrationSet(previous->id) == previous &&
                fixture.service.Calibration().selectedSetId == previous->id,
        "failed math retains the session and previous set without restarting acquisition");
    CaptureOsl(fixture, FIRST_PORT);
    const auto standardFrame = fixture.core.ReadView().frame;
    Complete(fixture);
    VerifyRestoredEntry(
        fixture, VnaService::TriggerSource::IMMEDIATE, VnaService::SweepMode::CONTINUOUS, standardFrame);
}

void RestoreFailureKeepsGeneratedSetAndFailsOperation()
{
    Fixture fixture;
    Require(Finish(fixture, fixture.service.RunContinuous("TEST")).ok, "prepare failing CONT restart");
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    fixture.device->ArmEnableFailure();
    CalibrationChange finish;
    finish.kind = CalibrationChangeKind::FINISH;
    finish.sessionId = fixture.service.Calibration().sessionId;
    const auto operation = Submit(fixture, finish);
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (!fixture.device->EnableEntered()) {
        fixture.service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "restore reaches the actual RF start barrier");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready && fixture.service.HasPending() &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD,
        "FINISH and OPC remain pending until core Start completes");
    const auto saved = fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId);
    Require(saved && !fixture.service.Calibration().collecting, "solved coefficients are retained before mode restart");
    fixture.device->ReleaseEnable();
    const auto result = Finish(fixture, operation);
    Require(!result.ok && result.message.find("restart failed") != std::string::npos &&
                fixture.core.ReadView().state == VnaCore::CoreState::FAULTED &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                fixture.service.FindCalibrationSet(saved->id) == saved && !fixture.service.Snapshot().correction,
        "resume hardware failure reports failure and HOLD without discarding the generated calibration set");
    const auto enables = fixture.device->EnableCount();
    fixture.service.Poll();
    fixture.service.Poll();
    Require(fixture.device->EnableCount() == enables, "faulted resume does not retry or automatically restart");
}

void FaultCancelDoesNotRestoreAcquisition()
{
    Fixture fixture;
    Require(Finish(fixture, fixture.service.RunContinuous("TEST")).ok, "prepare CONT before standard fault");
    Begin(fixture);
    fixture.device->Arm(true);
    const auto capture = Submit(fixture, CaptureChange(fixture, CalibrationStandard::OPEN, FIRST_PORT));
    fixture.device->Release();
    Require(!Finish(fixture, capture).ok, "standard acquisition faults");
    const auto enables = fixture.device->EnableCount();
    CalibrationChange cancel;
    cancel.kind = CalibrationChangeKind::CANCEL;
    cancel.sessionId = fixture.service.Calibration().sessionId;
    Require(Finish(fixture, Submit(fixture, cancel)).ok, "fault cancellation still clears the session");
    Require(!fixture.service.Calibration().collecting &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                fixture.core.ReadView().state == VnaCore::CoreState::FAULTED &&
                fixture.device->EnableCount() == enables,
        "fault cancellation never resumes the saved CONT acquisition");
}

void FirstResumedFrameFaultCannotCompleteSuccessfully()
{
    Fixture fixture;
    Require(Finish(fixture, fixture.service.RunContinuous("TEST")).ok, "prepare CONT before resumed frame fault");
    Begin(fixture);
    CaptureOsl(fixture, FIRST_PORT);
    fixture.device->Arm(true);
    CalibrationChange finish;
    finish.kind = CalibrationChangeKind::FINISH;
    finish.sessionId = fixture.service.Calibration().sessionId;
    const auto operation = Submit(fixture, finish);
    fixture.service.Poll();
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (!fixture.device->Entered()) {
        Require(std::chrono::steady_clock::now() < deadline, "resumed CONT reaches its first acquisition");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fixture.device->Release();
    while (fixture.core.ReadView().state != VnaCore::CoreState::FAULTED) {
        Require(std::chrono::steady_clock::now() < deadline, "first resumed acquisition publishes its fault");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Start 的成功 ticket 已就绪，但核心在服务消费它之前已经故障。
    const auto result = Finish(fixture, operation);
    Require(!result.ok && result.message.find("standard acquisition failed") != std::string::npos &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::HOLD &&
                fixture.service.FindCalibrationSet(fixture.service.Calibration().selectedSetId),
        "a successful Start ticket cannot report FINISH success after its actual core run already faulted");
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

int main(int argc, char **argv)
{
    unsigned failed = 0U;
    if (argc == 2 && std::string(argv[1]) == "restore") {
        Run("FINISH restores immediate CONT and actual acquisition", FinishRestoresContinuous, failed);
        Run("FINISH restores MANUAL CONT arming", FinishRestoresManualContinuous, failed);
        Run("FINISH restores MANUAL SINGLE arming", FinishRestoresManualSingle, failed);
        Run("FINISH preserves MANUAL HOLD", FinishPreservesManualHold, failed);
        Run("CANCEL restores entry modes", CancelRestoresEntryModes, failed);
        Run("failed solve preserves entry mode for retry", FailedSolveKeepsEntryModeForRetry, failed);
        Run("resume failure retains set and reports failure", RestoreFailureKeepsGeneratedSetAndFailsOperation, failed);
        Run("fault cancellation never restarts", FaultCancelDoesNotRestoreAcquisition, failed);
        Run("first resumed frame fault cannot report success", FirstResumedFrameFaultCannotCompleteSuccessfully,
            failed);
        return failed == 0U ? 0 : 1;
    }
    Run("single port fresh standard and raw read", OnePortWorkflowAndRawRead, failed);
    Run("arbitrary pair twelve-term source union", TwoPortArbitraryPairAndSourceUnion, failed);
    Run("queued guards and cancellation barrier", SessionLocksAndCancelBarrier, failed);
    Run("singular standards preserve previous set", SingularSessionPreservesCompletedSet, failed);
    Run("bounded import pool and physical compatibility", SetPoolImportAndStimulusCompatibility, failed);
    Run("failed recapture rejects retained old frame", FailedAcquisitionCannotCaptureOldFrame, failed);
    Run("old session rejection preserves current session", OldSessionFailureCannotAlterCurrentSession, failed);
    Run("accepted import freezes input aliases", AcceptedImportFreezesCallerAliases, failed);
    Run("confirmed standard preview is raw and read-only", StandardPreviewIsRawReadOnlyAndSessionScoped, failed);
    Run("FINISH restores immediate CONT and actual acquisition", FinishRestoresContinuous, failed);
    Run("FINISH restores MANUAL CONT arming", FinishRestoresManualContinuous, failed);
    Run("FINISH restores MANUAL SINGLE arming", FinishRestoresManualSingle, failed);
    Run("FINISH preserves MANUAL HOLD", FinishPreservesManualHold, failed);
    Run("CANCEL restores entry modes", CancelRestoresEntryModes, failed);
    Run("failed solve preserves entry mode for retry", FailedSolveKeepsEntryModeForRetry, failed);
    Run("resume failure retains set and reports failure", RestoreFailureKeepsGeneratedSetAndFailsOperation, failed);
    Run("fault cancellation never restarts", FaultCancelDoesNotRestoreAcquisition, failed);
    Run("first resumed frame fault cannot report success", FirstResumedFrameFaultCannotCompleteSuccessfully, failed);
    return failed == 0U ? 0 : 1;
}
