/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#include "fixture.h"

namespace {
using VnaServiceTest::Fixture;
const std::string OWNER = "SCPI";

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Wait(const std::function<bool()> &condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        Require(std::chrono::steady_clock::now() < deadline, "channel operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

VnaService::OperationResult Finish(Fixture &fixture, const VnaService::Operation &operation)
{
    Wait([&] {
        fixture.service.Poll();
        return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
    return operation.get();
}

void Done(Fixture &fixture, const VnaService::Operation &operation)
{
    const auto result = Finish(fixture, operation);
    Require(result.ok, result.message);
}

void Prepare(Fixture &fixture)
{
    Require(fixture.service.TryAcquireControl(OWNER), "acquire control");
    VnaService::ConfigPatch patch;
    patch.hasPoints = true;
    patch.points = 5U;
    patch.hasPowerDbm = true;
    patch.powerDbm = -7.25;
    Done(fixture, fixture.service.ApplyConfiguration(patch, fixture.service.Settings(1U).revision, OWNER, 1U));
}

unsigned Create(Fixture &fixture, bool copy = true, unsigned source = 1U)
{
    auto &service = fixture.service;
    VnaService::ChannelCreationOptions options;
    options.sourceChannelId = source;
    options.copyChannel = copy;
    Done(fixture, service.CreateChannel(options, service.Settings(source).revision, service.Display().revision, OWNER));
    return service.ActiveChannelId();
}

void Select(Fixture &fixture, unsigned id)
{
    Done(fixture, fixture.service.SelectChannel(id, fixture.service.Display().revision, OWNER));
}

void Manual(Fixture &fixture, unsigned id)
{
    Done(fixture, fixture.service.SelectTriggerSource(VnaService::TriggerSource::MANUAL, OWNER, id));
    Done(fixture, fixture.service.StartSweep(OWNER, id));
}

const VnaDisplay::TracePresentation &Trace(const VnaDisplay::DisplayState &display, const std::string &name)
{
    const auto found =
        std::find_if(display.traces.begin(), display.traces.end(), [&name](const VnaDisplay::TracePresentation &value) {
            return value.name == name;
        });
    Require(found != display.traces.end(), "trace presentation missing");
    return *found;
}

void CopyZoomAndSingleAreIndependent()
{
    Fixture fixture;
    Prepare(fixture);
    auto &service = fixture.service;
    VnaService::TraceCreationOptions extra;
    extra.parameter = "S11";
    Done(fixture, service.AddMeasurement(service.Settings(1U).revision, service.Display().revision, OWNER, extra, 1U));
    Require(service.SelectMeasurement("Trc1", 1U), "select source trace before copy");
    Manual(fixture, 1U);
    Done(fixture, service.Trigger(OWNER, 1U));
    VnaDisplay::DisplayChange marker;
    marker.kind = VnaDisplay::DisplayChangeKind::MARKER;
    marker.name = service.SelectedMeasurement(1U);
    marker.marker.action = VnaDisplay::MarkerAction::ADD;
    Done(fixture, service.ApplyDisplay(marker));
    marker.marker.action = VnaDisplay::MarkerAction::UPDATE;
    marker.marker.hasSampleIndex = true;
    marker.marker.sampleIndex = 2U;
    marker.marker.checkAcquisitionRevision = true;
    marker.marker.expectedAcquisitionRevision = service.Settings(1U).acquisitionRevision;
    Done(fixture, service.ApplyDisplay(marker));
    const auto original = service.ChannelSnapshot(1U);
    const auto originalDisplay = service.Display();
    const unsigned second = Create(fixture);
    const auto copied = service.ChannelSnapshot(second);
    Require(second == 2U && service.Snapshot().channels.size() == 2U && copied.settings.points == 5U &&
                copied.settings.powerDbm == -7.25 &&
                copied.settings.triggerSource == VnaService::TriggerSource::MANUAL &&
                copied.core.state == VnaCore::CoreState::RUNNING && !copied.core.frame &&
                copied.settings.measurements.size() == 2U && original.settings.measurements.size() == 2U &&
                copied.selectedMeasurement != original.selectedMeasurement,
        "copy must own independent settings and run");
    Require(
        Trace(service.Display(), copied.selectedMeasurement).markers.Items().front().enabled, "copy all trace markers");
    VnaService::StimulusZoomChange zoom;
    zoom.traceName = copied.selectedMeasurement;
    zoom.startHz = copied.settings.startHz + 1000000.0;
    zoom.stopHz = copied.settings.stopHz - 1000000.0;
    zoom.min = Trace(service.Display(), zoom.traceName).scale.min + 10.0;
    zoom.max = Trace(service.Display(), zoom.traceName).scale.max - 10.0;
    Done(fixture, service.ApplyStimulusZoom(zoom, copied.settings.revision, service.Display().revision, OWNER, second));
    Done(fixture, service.Trigger(OWNER, second));
    const auto zoomed = service.ChannelSnapshot(second);
    const auto unchanged = service.ChannelSnapshot(1U);
    Require(zoomed.core.frame && zoomed.core.frame->channelId == second &&
                zoomed.core.frame->plan.frequencyHz.front() == zoom.startHz &&
                zoomed.core.frame->plan.frequencyHz.back() == zoom.stopHz &&
                zoomed.core.frame->plan.frequencyHz.size() == 5U &&
                zoomed.core.frame->plan.powerDbm == std::vector<double>(5U, -7.25) && zoomed.stimulusZoom.depth == 1U,
        "zoom must use a genuine independent physical acquisition plan");
    Require(unchanged.core.frame == original.core.frame && unchanged.settings.revision == zoomed.settings.revision &&
                unchanged.settings.acquisitionRevision == original.settings.acquisitionRevision &&
                unchanged.core.runId == original.core.runId && unchanged.stimulusZoom.depth == 0U &&
                Trace(service.Display(), original.selectedMeasurement).markers.Items().front().frequencyHz ==
                    Trace(originalDisplay, original.selectedMeasurement).markers.Items().front().frequencyHz,
        "changing Ch2 must preserve Ch1 complete frame, identity, markers and history");
    const auto manualFrame = zoomed.core.frame;
    zoom.startHz = zoomed.settings.startHz;
    zoom.stopHz = zoomed.settings.stopHz;
    zoom.min += 1.0;
    zoom.max -= 1.0;
    Done(fixture, service.ApplyStimulusZoom(zoom, zoomed.settings.revision, service.Display().revision, OWNER, second));
    Require(service.ChannelSnapshot(second).core.frame == manualFrame &&
                service.Settings(second).acquisitionRevision == zoomed.settings.acquisitionRevision &&
                service.ChannelSnapshot(second).core.runId == zoomed.core.runId,
        "Y-only manual zoom must preserve the complete frame and acquisition identity");
    VnaService::StimulusZoomChange out;
    out.action = VnaService::StimulusZoomAction::OUT;
    Done(fixture,
        service.ApplyStimulusZoom(out, service.Settings(second).revision, service.Display().revision, OWNER, second));
    Require(service.ChannelSnapshot(second).core.frame == manualFrame, "Y-only undo must not reconfigure or trigger");
    zoom.startHz += 1000000.0;
    zoom.stopHz -= 1000000.0;
    Done(fixture,
        service.ApplyStimulusZoom(zoom, service.Settings(second).revision, service.Display().revision, OWNER, second));
    Require(Trace(service.Display(), original.selectedMeasurement).markers.Items().front().acquisitionRevision ==
                Trace(originalDisplay, original.selectedMeasurement).markers.Items().front().acquisitionRevision,
        "axis/version reconciliation must not rebind another channel's discrete marker");
    Done(fixture, service.RunSelectedSingle(OWNER, second));
    Done(fixture, service.Trigger(OWNER, second));
    Require(service.ChannelSnapshot(second).core.state == VnaCore::CoreState::READY &&
                service.ChannelSnapshot(1U).core.state == VnaCore::CoreState::RUNNING &&
                service.ChannelSnapshot(1U).core.frame == original.core.frame,
        "single must stop only its owner channel");
    Select(fixture, 1U);
    Require(
        service.MeasurementData(false, second).ok && service.DisplayData(copied.selectedMeasurement, second).available,
        "explicit queries must use the owner frame while another channel is selected");
    Require(!service.DisplayData(copied.selectedMeasurement, 1U).available, "reject trace/channel mismatch");
}

void CalibrationAndFiniteAdmissionAreScoped()
{
    Fixture fixture;
    Prepare(fixture);
    auto &service = fixture.service;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->frequencyHz = service.StimulusAxis(1U).values;
    VnaCalibration::OnePortTerms identity;
    identity.reflectionTracking = {1.0, 0.0};
    data->onePortTerms.assign(data->frequencyHz.size(), identity);
    auto imported = std::make_shared<VnaService::CalibrationSet>();
    imported->name = "owner calibration";
    imported->deviceIdentity = service.Identity();
    imported->ifBandwidthHz = service.Settings(1U).ifBandwidthHz;
    imported->powerDbm.assign(data->frequencyHz.size(), service.Settings(1U).powerDbm);
    imported->data = data;
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    change.importedSet = imported;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(1U).revision, OWNER, 1U));
    change = {};
    change.kind = VnaService::CalibrationChangeKind::SELECT;
    change.setId = service.Calibration(1U).sets.front().id;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(1U).revision, OWNER, 1U));
    change.kind = VnaService::CalibrationChangeKind::ENABLE;
    change.enabled = true;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(1U).revision, OWNER, 1U));
    const auto source = service.ChannelSnapshot(1U);
    Require(source.correction && source.calibration.compatible, "source calibration enabled");
    const auto second = Create(fixture);
    Require(service.ChannelSnapshot(second).correction == source.correction,
        "copy shares only immutable coefficient snapshot");
    change.enabled = false;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(second).revision, OWNER, second));
    Require(!service.Calibration(second).enabled && service.Calibration(1U).enabled,
        "copied workspace selection is independent");
    change.enabled = true;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(second).revision, OWNER, second));
    Done(fixture, service.SetPowerDbm(-3.75, OWNER, second));
    Require(!service.Calibration(second).compatible && !service.ChannelSnapshot(second).correction &&
                service.Calibration(1U).compatible && service.ChannelSnapshot(1U).correction == source.correction,
        "changed Power invalidates only the target channel calibration compatibility");
    Manual(fixture, 1U);
    Manual(fixture, second);
    Done(fixture, service.Trigger(OWNER, 1U));
    fixture.device->Arm();
    const auto trigger = service.Trigger(OWNER, second);
    Wait([&] {
        service.Poll();
        return fixture.device->HasEntered();
    });
    Require(service.ChannelSnapshot(second).finitePending && !service.ChannelSnapshot(1U).finitePending &&
                service.MeasurementData(false, 1U).ok,
        "another channel's finite task must not invalidate this complete frame");
    fixture.device->Release();
    Done(fixture, trigger);
    change = {};
    change.kind = VnaService::CalibrationChangeKind::BEGIN;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(1U).revision, OWNER, 1U));
    const auto session1 = service.Calibration(1U).sessionId;
    Require(!Finish(fixture, service.SelectChannel(second, service.Display().revision, OWNER)).ok &&
                !Finish(fixture, service.SetPowerDbm(-2.0, OWNER, second)).ok &&
                !Finish(fixture, service.StartSweep(OWNER, second)).ok &&
                !Finish(fixture,
                    service.CreateChannel({}, service.Settings(1U).revision, service.Display().revision, OWNER))
                     .ok,
        "calibration collection globally locks channel switching, configuration and starting");
    change.kind = VnaService::CalibrationChangeKind::CANCEL;
    change.sessionId = session1;
    Require(!Finish(fixture, service.ApplyCalibration(change, service.Calibration(second).revision, OWNER, second)).ok,
        "calibration session cannot be cancelled through another channel");
    Done(fixture, service.ApplyCalibration(change, service.Calibration(1U).revision, OWNER, 1U));
    change = {};
    change.kind = VnaService::CalibrationChangeKind::BEGIN;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(second).revision, OWNER, second));
    Require(service.Calibration(second).sessionId != session1 &&
                !service.CalibrationPreview(session1, "OPEN1", second).available,
        "session and preview identities are namespaced per channel");
    change.kind = VnaService::CalibrationChangeKind::CANCEL;
    change.sessionId = service.Calibration(second).sessionId;
    Done(fixture, service.ApplyCalibration(change, service.Calibration(second).revision, OWNER, second));
}

void NewDefaultsContinuousFairnessAndPower()
{
    Fixture fixture;
    Prepare(fixture);
    auto &service = fixture.service;
    const auto first = service.ChannelSnapshot(1U);
    const auto id = Create(fixture, false);
    const auto created = service.ChannelSnapshot(id);
    Require(created.settings.points == service.Profile().defaultPoints &&
                created.settings.startHz == service.Profile().defaultStartHz &&
                created.settings.powerDbm == service.Profile().defaultPowerDbm &&
                created.settings.measurements.size() == 1U && created.core.state == VnaCore::CoreState::RUNNING,
        "new channel uses defaults and starts continuous");
    Done(fixture, service.RunContinuous(OWNER, 1U));
    for (double power : {-120.0, -2.75, 100.0}) {
        Done(fixture, service.SetPowerDbm(power, OWNER, id));
        const auto revision = service.Settings(id).acquisitionRevision;
        Wait([&] {
            service.Poll();
            const auto view = service.ChannelSnapshot(id).core;
            return view.frame && view.frame->configRevision == revision;
        });
        const auto frame = service.ChannelSnapshot(id).core.frame;
        Require(frame->plan.powerDbm == std::vector<double>(created.settings.points, power),
            "physical plan must contain the selected power at every point");
    }
    const auto before = service.ChannelSnapshot(id);
    for (double power :
        {-120.1, 100.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        Require(!Finish(fixture, service.SetPowerDbm(power, OWNER, id)).ok, "invalid power rejected");
        Require(service.Settings(id).revision == before.settings.revision, "invalid power cannot change revision");
    }
    Wait([&] {
        service.Poll();
        const auto one = service.ChannelSnapshot(1U).core;
        const auto two = service.ChannelSnapshot(id).core;
        return one.frame && two.frame && one.frame->frameSeq >= 2U && two.frame->frameSeq >= 2U;
    });
    const auto one = service.ChannelSnapshot(1U);
    Require(one.settings.acquisitionRevision == first.settings.acquisitionRevision && one.core.frame->channelId == 1U &&
                one.core.frame->plan.frequencyHz.size() == 5U &&
                one.core.frame->plan.powerDbm == std::vector<double>(5U, -7.25),
        "fair physical scheduling must retain the other channel's own points and power");
    Require(!service.SetCurrentTriggerScope(false, id), "all-channel trigger cannot be silently simulated");
}

void MatrixEmptyChannelFifoAndDeletion()
{
    Fixture fixture;
    Prepare(fixture);
    auto &service = fixture.service;
    Done(fixture, service.RunSingle(OWNER, 1U));
    const auto first = service.ChannelSnapshot(1U);
    const auto second = Create(fixture);
    Done(fixture,
        service.CreateAllSParameters(service.Settings(second).revision, service.Display().revision, OWNER, second));
    Require(service.ChannelSnapshot(1U).core.frame == first.core.frame &&
                service.MeasurementChannel(first.selectedMeasurement) == 1U &&
                service.ChannelSnapshot(second).settings.measurements.size() ==
                    service.Profile().portCount * service.Profile().portCount,
        "All S-Params must replace only its target channel");
    for (const auto &measurement : service.ChannelSnapshot(second).settings.measurements) {
        Require(measurement.channelId == second && measurement.name != first.selectedMeasurement,
            "matrix ownership and names");
        Done(fixture, service.DeleteMeasurement(measurement.name, 0U, OWNER, second));
    }
    Require(service.ChannelSnapshot(second).settings.measurements.empty(), "empty channel is supported");
    Select(fixture, 1U);
    Select(fixture, second);
    Require(service.ActiveChannelId() == second && service.SelectedMeasurement(second).empty(),
        "Channel Manager selects an empty channel");
    Done(fixture, service.SetPowerDbm(-3.5, OWNER, second));
    Done(fixture,
        service.AddMeasurement(service.Settings(second).revision, service.Display().revision, OWNER, {}, second));
    const auto select = service.SelectChannel(1U, service.Display().revision, OWNER);
    VnaService::ConfigPatch patch;
    patch.hasPoints = true;
    patch.points = 9U;
    const auto configure = service.ApplyConfiguration(patch, service.Settings(second).revision, OWNER, second);
    Done(fixture, select);
    Done(fixture, configure);
    Require(
        service.ActiveChannelId() == 1U && service.Settings(1U).points == 5U && service.Settings(second).points == 9U,
        "FIFO tasks freeze explicit channel before a preceding selection changes active context");
    const auto stale = service.CreateChannel({}, service.Settings(1U).revision, service.Display().revision - 1U, OWNER);
    Require(!Finish(fixture, stale).ok && service.Snapshot().channels.size() == 2U, "channel creation double CAS");
    fixture.device->FailNextPlanPreparation();
    Require(
        !Finish(fixture, service.CreateChannel({}, service.Settings(1U).revision, service.Display().revision, OWNER))
                .ok &&
            service.Snapshot().channels.size() == 2U,
        "failed creation must not publish service channel or catalog");
    Done(fixture, service.DeleteChannel(1U, service.Settings(1U).revision, service.Display().revision, OWNER));
    Require(!service.HasChannel(1U) && service.ActiveChannelId() == second,
        "first channel can be removed while another survives");
    Require(!Finish(fixture,
                service.DeleteChannel(second, service.Settings(second).revision, service.Display().revision, OWNER))
                 .ok,
        "last channel protected");
}

void GlobalLogicalRevisionAndIndependentAcquisition()
{
    Fixture fixture;
    Prepare(fixture);
    auto &service = fixture.service;
    Done(fixture, service.RunSingle(OWNER, 1U));
    const auto second = Create(fixture);
    Select(fixture, 1U);
    const auto before = service.Snapshot();
    const auto firstFrame = service.ChannelSnapshot(1U).core.frame;
    const auto firstAcquisition = service.Settings(1U).acquisitionRevision;
    VnaService::ConfigPatch patch;
    patch.hasPowerDbm = true;
    patch.powerDbm = -8.5;
    Done(fixture, service.ApplyConfiguration(patch, before.settings.revision, OWNER, second));
    const auto updated = service.Snapshot();
    Require(updated.settings.revision == before.settings.revision + 1U &&
                service.Settings(1U).revision == updated.settings.revision &&
                service.Settings(second).revision == updated.settings.revision &&
                service.Snapshot(second).settings.revision == updated.settings.revision &&
                service.ChannelSnapshot(1U).core.frame == firstFrame &&
                service.Settings(1U).acquisitionRevision == firstAcquisition,
        "non-active channel accepts top-level global CAS without invalidating another physical frame");
    for (const auto &channel : updated.channels) {
        Require(channel.settings.revision == updated.settings.revision,
            "all channel projections share one logical version");
    }
    Require(!Finish(fixture, service.ApplyConfiguration(patch, before.settings.revision, OWNER, 1U)).ok,
        "a successful write to another channel invalidates an old global CAS");
    Select(fixture, second);
    Select(fixture, 1U);
    Require(service.Snapshot().settings.revision == updated.settings.revision,
        "selection never rolls global revision back");
    const auto target = service.ChannelSnapshot(second);
    VnaService::StimulusZoomChange zoom;
    zoom.traceName = target.selectedMeasurement;
    zoom.startHz = target.settings.startHz + 1000000.0;
    zoom.stopHz = target.settings.stopHz - 1000000.0;
    zoom.min = Trace(service.Display(), zoom.traceName).scale.min + 1.0;
    zoom.max = Trace(service.Display(), zoom.traceName).scale.max - 1.0;
    Done(fixture, service.ApplyStimulusZoom(
                      zoom, service.Snapshot().settings.revision, service.Display().revision, OWNER, second));
    Require(service.ActiveChannelId() == 1U && service.Settings(second).revision == updated.settings.revision + 1U &&
                service.ChannelSnapshot(1U).core.frame == firstFrame &&
                service.Settings(1U).acquisitionRevision == firstAcquisition,
        "non-active Zoom uses global CAS while only its own acquisition revision changes");
}
}  // namespace

int main()
{
    try {
        CopyZoomAndSingleAreIndependent();
        NewDefaultsContinuousFairnessAndPower();
        MatrixEmptyChannelFifoAndDeletion();
        CalibrationAndFiniteAdmissionAreScoped();
        GlobalLogicalRevisionAndIndependentAcquisition();
        std::cout << "channel service tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
