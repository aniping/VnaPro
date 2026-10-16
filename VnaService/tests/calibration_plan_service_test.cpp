/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <future>
#include <iostream>
#include <thread>

#include "calibration_plan_test_support.h"
#include "fixture.h"

namespace {
using namespace CalibrationPlanTest;
using VnaServiceTest::Fixture;
const std::string OWNER = "SCPI";

VnaService::InstrumentProfile Profile()
{
    VnaService::InstrumentProfile result;
    result.defaultStopHz = 10004000.0;
    result.defaultPoints = 5U;
    return result;
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "calibration plan service operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get();
}

VnaService::OperationResult Change(Fixture &fixture, VnaService::CalibrationChange change)
{
    return Finish(
        fixture, fixture.service.ApplyCalibration(std::move(change), fixture.service.Calibration().revision, OWNER));
}

VnaService::CalibrationSetPtr Prepare(Fixture &fixture)
{
    Require(fixture.service.TryAcquireControl(OWNER), "acquire calibration plan controller");
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    change.importedSet = Source(fixture.service.Identity());
    Require(Change(fixture, change).ok, "import nonconstant validated coefficients");
    change = {};
    change.kind = VnaService::CalibrationChangeKind::ACTIVATE;
    change.setId = 1U;
    Require(Change(fixture, change).ok, "activate on actual committed sweep axis");
    return fixture.service.FindCalibrationSet(1U);
}

void SweepChangesPrepareBeforePublishing()
{
    Fixture fixture(Profile());
    const auto source = Prepare(fixture);
    auto &service = fixture.service;
    Require(service.Snapshot().correction == source->data && service.Calibration().effective,
        "same-axis activation must share the original frozen coefficient pointer");
    Require(Finish(fixture, service.SetPoints(3U)).ok, "prepare exact source-axis subset");
    const auto subset = service.Snapshot().correction;
    Require(subset && subset != source->data && service.Calibration().effective &&
                service.Calibration().applicability.frequencyMode == VnaService::CalibrationFrequencyMode::SUBSET,
        "point count change must publish a prepared subset and effective status together");
    Require(Finish(fixture, service.SetPoints(4U)).ok && service.Calibration().effective &&
                service.Calibration().applicability.frequencyMode == VnaService::CalibrationFrequencyMode::INTERPOLATED,
        "new actual Hz points must publish explicit interpolation state");
    Require(Finish(fixture, service.SetPoints(3U)).ok && service.Snapshot().correction == subset,
        "returning to a prepared target axis must reuse the bounded cache");
}

void RangeAndBandwidthChangesRetainIntent()
{
    Fixture fixture(Profile());
    Prepare(fixture);
    auto &service = fixture.service;
    Require(Finish(fixture, service.SetPoints(3U)).ok, "prepare exact subset");
    const auto subset = service.Snapshot().correction;
    Require(
        Finish(fixture, service.SetStopHz(10005000.0)).ok && service.Calibration().enabled &&
            !service.Calibration().effective && !service.Snapshot().correction &&
            service.Calibration().applicability.reasonCode == VnaService::CalibrationApplicabilityReason::OUT_OF_RANGE,
        "expanding outside the calibration range must disable all correction while retaining enable intent");
    Require(Finish(fixture, service.SetStopHz(10004000.0)).ok && service.Snapshot().correction == subset,
        "restoring an admissible sweep must restore correction without activation or disk loading");
    Require(Finish(fixture, service.SetIfBandwidthHz(10000.0)).ok && service.Calibration().effective &&
                service.Snapshot().correction == subset && !service.Calibration().applicability.warnings.empty(),
        "IFBW changes must preserve effective coefficients with an explicit warning");
}

void RejectedEnableAndFailedConfigurationAreAtomic()
{
    Fixture fixture(Profile());
    const auto source = Prepare(fixture);
    auto &service = fixture.service;
    const auto original = service.Snapshot();
    fixture.device->FailNextPlanPreparation();
    Require(!Finish(fixture, service.SetPoints(3U)).ok && service.Snapshot().correction == original.correction &&
                service.Settings().revision == original.settings.revision && service.Calibration().effective,
        "failed device configuration must not publish a prepared candidate or its calibration state");
    Require(Finish(fixture, service.SetPowerDbm(-1.0, OWNER)).ok, "change source power");
    const auto blocked = service.Calibration();
    VnaService::CalibrationChange enable;
    enable.kind = VnaService::CalibrationChangeKind::ENABLE;
    enable.enabled = true;
    Require(blocked.enabled && !blocked.effective && !Change(fixture, enable).ok &&
                service.Calibration().revision == blocked.revision && service.Calibration().selectedSetId == 1U,
        "enabling incompatible correction must fail atomically with no misleading active state");
    Require(Finish(fixture, service.SetPowerDbm(0.0, OWNER)).ok && service.Calibration().effective &&
                service.Snapshot().correction == source->data,
        "restoring power must recover the original enabled correction automatically");
}

void ZoomResetAndChannelCopyPrepareOwnResults()
{
    Fixture fixture(Profile());
    const auto source = Prepare(fixture);
    auto &service = fixture.service;
    Require(Finish(fixture, service.SetPoints(3U)).ok, "set exact subset point count");
    auto before = service.Snapshot();
    VnaService::StimulusZoomChange zoom;
    zoom.traceName = before.display.selectedMeasurement;
    zoom.startHz = 10001000.0;
    zoom.stopHz = 10003000.0;
    zoom.min = before.display.traces.front().scale.min + 1.0;
    zoom.max = before.display.traces.front().scale.max - 1.0;
    Require(
        Finish(fixture, service.ApplyStimulusZoom(zoom, before.settings.revision, before.display.revision, OWNER)).ok &&
            service.Calibration().effective &&
            service.Calibration().applicability.frequencyMode == VnaService::CalibrationFrequencyMode::SUBSET &&
            service.Snapshot().correction->frequencyHz == service.StimulusAxis().values,
        "zoom must prepare its actual target axis before the atomic display/configuration commit");
    Require(Finish(fixture, service.ResetMeasurements(OWNER)).ok && service.Calibration().effective &&
                service.Snapshot().correction == source->data,
        "reset must retain the original pool and restore the prepared default-axis correction");
    VnaService::ChannelCreationOptions options;
    options.copyChannel = true;
    options.sourceChannelId = 1U;
    before = service.Snapshot();
    Require(
        Finish(fixture, service.CreateChannel(options, before.settings.revision, before.display.revision, OWNER)).ok,
        "copy calibrated channel");
    Require(service.ChannelSnapshot(2U).correction == source->data,
        "channel copy must share immutable coefficients while keeping its own state");
    Require(Finish(fixture, service.SetPowerDbm(-2.0, OWNER, 2U)).ok && !service.Calibration(2U).effective &&
                service.Calibration(1U).effective && service.ChannelSnapshot(1U).correction == source->data,
        "a copied channel's invalid stimulus must not poison the original channel cache");
}

void PersistenceFailureRetainsCommittedPreparation()
{
    Fixture fixture(Profile());
    bool fail = false;
    fixture.service.SetCalibrationSaveHandler(
        [&fail](unsigned, const std::vector<VnaService::CalibrationSetPtr> &, std::uint64_t) {
            std::promise<VnaService::OperationResult> promise;
            VnaService::OperationResult result;
            result.ok = !fail;
            result.message = fail ? "injected save failure" : "";
            promise.set_value(result);
            return promise.get_future().share();
        });
    const auto source = Prepare(fixture);
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    change.importedSet = Source(fixture.service.Identity());
    Require(Change(fixture, change).ok, "import second independently frozen set");
    Require(Finish(fixture, fixture.service.SetPoints(3U)).ok, "prepare selected subset");
    const auto before = fixture.service.Snapshot();
    fail = true;
    change = {};
    change.kind = VnaService::CalibrationChangeKind::ACTIVATE;
    change.setId = 2U;
    Require(!Change(fixture, change).ok && fixture.service.Snapshot().correction == before.correction &&
                fixture.service.Calibration().revision == before.calibration.revision &&
                fixture.service.Calibration().selectedSetId == before.calibration.selectedSetId &&
                fixture.service.FindCalibrationSet(1U) == source,
        "failed durable activation must retain selection, revision and committed derived correction");
}
}  // 匿名命名空间

int main()
{
    try {
        SweepChangesPrepareBeforePublishing();
        RangeAndBandwidthChangesRetainIntent();
        RejectedEnableAndFailedConfigurationAreAtomic();
        ZoomResetAndChannelCopyPrepareOwnResults();
        PersistenceFailureRetainsCommittedPreparation();
        std::cout << "calibration plan service tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
