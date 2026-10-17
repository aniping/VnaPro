/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "fixture.h"

namespace {
using VnaServiceTest::Fixture;

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Sweep timing transaction timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get();
}

VnaService::ConfigPatch Manual(double seconds)
{
    VnaService::ConfigPatch patch;
    patch.hasSweepTimeSeconds = true;
    patch.sweepTimeSeconds = seconds;
    return patch;
}

VnaService::OperationResult Apply(Fixture &fixture, VnaService::ConfigPatch patch)
{
    return Finish(fixture, fixture.service.ApplyConfiguration(patch, fixture.service.Settings().revision, "SCPI"));
}

void ManualTimingChangesRealPlanAndAutoRetainsRequest()
{
    Fixture fixture;
    Require(fixture.service.Settings().sweepTimeAuto, "Default timing must remain Auto");
    Require(fixture.service.TryAcquireControl(), "Acquire timing controller");
    const auto before = fixture.service.Settings();
    Require(Apply(fixture, Manual(0.25)).ok, "Commit a manual timing request");
    const auto changed = fixture.service.Settings();
    Require(!changed.sweepTimeAuto && changed.sweepTimeSeconds == 0.25, "Numeric time setter must turn Auto off");
    Require(changed.revision == before.revision + 1U, "Timing advances logical revision once");
    Require(changed.acquisitionRevision == before.acquisitionRevision + 1U, "Timing advances real plan revision");
    Require(Finish(fixture, fixture.service.RunSingle("SCPI")).ok, "Acquire a complete configuration-domain sample");
    const auto frame = fixture.core.ReadView().frame;
    Require(frame && !frame->plan.sweepTimeAuto && frame->plan.sweepTimeSeconds == 0.25,
        "Manual timing must reach the acquired physical plan");
    auto patch = Manual(0.5);
    patch.hasSweepTimeAuto = true;
    patch.sweepTimeAuto = true;
    Require(Apply(fixture, patch).ok, "Explicit Auto takes priority independent of JSON field order");
    Require(fixture.service.Settings().sweepTimeAuto && fixture.service.Settings().sweepTimeSeconds == 0.5,
        "Auto retains the last manual request without applying pacing");
}

void InvalidRequestsAndControlConflictsDoNotCommit()
{
    Fixture fixture;
    const auto initial = fixture.service.Settings();
    Require(!Apply(fixture, Manual(0.2)).ok, "Timing writes require control");
    Require(fixture.service.TryAcquireControl(), "Acquire timing controller");
    for (const double value :
        {-1.0, 0.0, 100000.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        Require(!Apply(fixture, Manual(value)).ok, "Invalid duration must be rejected");
        Require(fixture.service.Settings().revision == initial.revision &&
                    fixture.service.Settings().acquisitionRevision == initial.acquisitionRevision,
            "Invalid duration cannot partially change either revision");
    }
    auto patch = Manual(0.3);
    const auto stale = fixture.service.ApplyConfiguration(patch, initial.revision + 1U, "SCPI");
    Require(!Finish(fixture, stale).ok, "Stale expected revision rejects timing");
    VnaService::CalibrationChange begin;
    begin.kind = VnaService::CalibrationChangeKind::BEGIN;
    Require(Finish(fixture, fixture.service.ApplyCalibration(begin)).ok, "Begin calibration collection");
    Require(!Apply(fixture, patch).ok, "Calibration collection locks timing just like other sweep configuration");
}

void CopyNewAndResetKeepTimingOwnership()
{
    Fixture fixture;
    Require(fixture.service.TryAcquireControl(), "Acquire timing controller");
    Require(Apply(fixture, Manual(0.25)).ok, "Set channel one timing");
    VnaService::ChannelCreationOptions options;
    options.sourceChannelId = 1U;
    options.copyChannel = true;
    Require(Finish(fixture, fixture.service.CreateChannel(options, fixture.service.Settings().revision,
                                fixture.service.Display().revision, "SCPI"))
                .ok,
        "Copy timing channel");
    Require(!fixture.service.Settings(2U).sweepTimeAuto && fixture.service.Settings(2U).sweepTimeSeconds == 0.25,
        "Channel copy retains its own timing request");
    options.copyChannel = false;
    Require(Finish(fixture, fixture.service.CreateChannel(options, fixture.service.Settings().revision,
                                fixture.service.Display().revision, "SCPI"))
                .ok,
        "Create independent channel");
    Require(fixture.service.Settings(3U).sweepTimeAuto && fixture.service.Settings(3U).sweepTimeSeconds == 1.0,
        "New channel starts Auto without inheriting another channel's pacing");
    Require(Finish(fixture, fixture.service.ResetMeasurements("SCPI", 2U)).ok, "Reset copied channel");
    Require(fixture.service.Settings(2U).sweepTimeAuto && fixture.service.Settings(2U).sweepTimeSeconds == 1.0,
        "Preset resets timing to Auto");
    Require(!fixture.service.Settings(1U).sweepTimeAuto, "Another channel's reset cannot change channel one timing");
}

void ZoomAndUndoRetainManualTiming()
{
    Fixture fixture;
    Require(fixture.service.TryAcquireControl(), "Acquire timing controller");
    Require(Apply(fixture, Manual(0.25)).ok, "Set zoom channel timing");
    const auto before = fixture.service.Snapshot();
    VnaService::StimulusZoomChange zoom;
    zoom.traceName = before.display.selectedMeasurement;
    zoom.startHz = 20000000.0;
    zoom.stopHz = 40000000.0;
    zoom.min = -70.0;
    zoom.max = 0.0;
    Require(Finish(fixture,
                fixture.service.ApplyStimulusZoom(zoom, before.settings.revision, before.display.revision, "SCPI"))
                .ok,
        "Apply zoom with timing request");
    const auto changed = fixture.service.Snapshot();
    Require(!changed.settings.sweepTimeAuto && changed.settings.sweepTimeSeconds == 0.25,
        "Zoom cannot reset software sweep timing");
    zoom.action = VnaService::StimulusZoomAction::OUT;
    Require(Finish(fixture,
                fixture.service.ApplyStimulusZoom(zoom, changed.settings.revision, changed.display.revision, "SCPI"))
                .ok,
        "Undo zoom with timing request");
    Require(!fixture.service.Settings().sweepTimeAuto && fixture.service.Settings().sweepTimeSeconds == 0.25,
        "Zoom undo cannot replace timing fields it did not own");
}

VnaService::CalibrationSetPtr Source(Fixture &fixture)
{
    VnaService::CalibrationSet set;
    set.id = 1U;
    set.name = "Timing cache reuse";
    set.deviceIdentity = fixture.service.Identity();
    set.ifBandwidthHz = fixture.service.Settings().ifBandwidthHz;
    const auto axis = fixture.service.StimulusAxis().values;
    set.powerDbm.assign(axis.size(), fixture.service.Settings().powerDbm);
    VnaCalibration::CalibrationData data;
    data.frequencyHz = axis;
    data.onePortTerms.assign(axis.size(), {{0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}});
    set.data = std::make_shared<const VnaCalibration::CalibrationData>(std::move(data));
    return std::make_shared<const VnaService::CalibrationSet>(std::move(set));
}

void TimingReusesPreparedCalibrationWithoutPersistence()
{
    Fixture fixture;
    std::string error;
    Require(fixture.service.RestoreCalibration({Source(fixture)}, 1U, error), error);
    Require(fixture.service.TryAcquireControl(), "Acquire timing controller");
    VnaService::CalibrationChange enable;
    enable.kind = VnaService::CalibrationChangeKind::ENABLE;
    enable.enabled = true;
    Require(Finish(fixture, fixture.service.ApplyCalibration(enable)).ok, "Enable prepared calibration");
    const auto before = fixture.service.ChannelSnapshot(1U);
    Require(before.correction != nullptr, "Selected calibration must already be prepared");
    const auto source = fixture.service.FindCalibrationSet(1U);
    Require(Apply(fixture, Manual(0.25)).ok, "Change time with calibration active");
    const auto changed = fixture.service.ChannelSnapshot(1U);
    Require(changed.correction == before.correction && fixture.service.FindCalibrationSet(1U) == source,
        "Timing-only change must reuse original and working calibration pointers");
    Require(changed.calibration.revision == before.calibration.revision && changed.calibration.effective,
        "Timing-only change cannot change calibration revision or turn correction off");
}
}  // 匿名命名空间

int main()
{
    try {
        ManualTimingChangesRealPlanAndAutoRetainsRequest();
        InvalidRequestsAndControlConflictsDoNotCommit();
        CopyNewAndResetKeepTimingOwnership();
        ZoomAndUndoRetainManualTiming();
        TimingReusesPreparedCalibrationWithoutPersistence();
        std::cout << "PASS sweep timing configuration, ownership, revisions, zoom and calibration reuse\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
