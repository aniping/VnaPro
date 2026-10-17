/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "sweep_progress_test_support.h"
#include "web/frame_array_json.h"
#include "web/json_encoder.h"

namespace {
using namespace SweepProgressTest;
using VnaWeb::Json;

void CheckFirstFrameMetadata(const Json &frame)
{
    Require(Field(frame, "available").boolean && !Field(frame, "current").boolean,
        "Single's finite pending operation must still expose display-only progress");
    Require(Field(frame, "frameSeq").string == "1" && Field(frame, "runId").string == "3" &&
                Field(frame, "revision").string == "7" && Field(frame, "calibrationRevision").string == "9" &&
                Field(frame, "displayRevision").string == "6",
        "Progress retains all projection identities");
    Require(Field(frame, "originalPointCount").number == 4.0 && Field(frame, "displayPointCount").number == 4.0 &&
                !Field(frame, "decimated").boolean && Field(frame, "frequencyHz").array.size() == 4U,
        "Progress carries the full physical sweep axis without decimation");
}

void CheckCorrectedPrefix(const Json &frame)
{
    const auto &trace = Field(frame, "traces").array.front();
    Require(Field(trace, "calibration").string == "calibrated" &&
                std::abs(Field(trace, "real").array.front().number - 0.25) < 1e-12,
        "Prefix must use actual measurement and calibration, not raw IQ or an animation");
    for (std::size_t index = 0U; index < 4U; ++index) {
        Require(Field(frame, "sampleIndexes").array[index].number == static_cast<double>(index),
            "Progress sample indexes must be original physical indexes");
        Require(Field(trace, "valid").array[index].boolean == (index < 2U), "Tail cannot become valid zero samples");
        if (index >= 2U) {
            Require(Field(trace, "real").array[index].type == Json::Type::NULL_VALUE &&
                        Field(trace, "imag").array[index].type == Json::Type::NULL_VALUE &&
                        Field(Field(trace, "projection"), "values").array[index].type == Json::Type::NULL_VALUE,
                "Unmeasured suffix must remain null throughout IQ and display projection");
        }
    }
}

void FirstSlowSweepHasRealCorrectedPrefix()
{
    auto snapshot = Snapshot();
    snapshot.channels.front().finitePending = true;
    const auto response = Response(snapshot);
    Require(!Field(response, "available").boolean, "No partial IQ may become the first completed frame");
    const auto &progress = Field(response, "sweepProgress");
    Require(Field(progress, "completedPoints").number == 2.0, "Only real completed prefix is exposed");
    const auto &frame = Field(progress, "frame");
    CheckFirstFrameMetadata(frame);
    CheckCorrectedPrefix(frame);
}

void ProgressCannotPublishMarkerReadouts()
{
    const auto response = Response(Snapshot());
    const auto &trace = Field(Field(Field(response, "sweepProgress"), "frame"), "traces").array.front();
    const auto &projection = Field(trace, "projection");
    Require(!Field(Field(projection, "marker"), "available").boolean &&
                !Field(Field(projection, "marker"), "valid").boolean,
        "Even a marker on the measured prefix cannot become a formal readout");
    for (const auto &marker : Field(projection, "markers").array) {
        Require(!Field(marker, "available").boolean && !Field(marker, "valid").boolean &&
                    Field(marker, "real").type == Json::Type::NULL_VALUE,
            "All progress marker numbers must remain unavailable");
    }
}

void CompleteFrameRemainsExactAndIndependent()
{
    auto snapshot = Snapshot();
    auto complete = Frame(0.2);
    complete.runId = 2U;
    complete.frameSeq = 8U;
    snapshot.channels.front().core.frame = std::make_shared<const VnaCore::MeasurementFrame>(complete);
    auto response = Response(snapshot);
    const auto &trace = Field(response, "traces").array.front();
    Require(Field(response, "available").boolean && Field(response, "frameSeq").string == "8" &&
                Field(response, "runId").string == "2" && Field(trace, "valid").array.back().boolean &&
                std::abs(Field(trace, "real").array.back().number - 0.05) < 1e-12,
        "Prior completed sweep must retain its full corrected values and identity");
    Require(Field(Field(Field(trace, "projection"), "marker"), "available").boolean,
        "Only completed data may retain formal marker readouts");
    Require(Field(Field(response, "sweepProgress"), "frame").Find("traces") != nullptr,
        "A new run's first progress must coexist with the prior completed frame");
    response.object.erase("sweepProgress");
    snapshot.channels.front().core.sweepProgress.reset();
    Require(VnaWeb::StringifyJson(response) == VnaWeb::StringifyJson(Response(snapshot)),
        "Adding progress cannot change any completed frame field");
}

void SameRunRequiresNextFrameSequence()
{
    auto snapshot = Snapshot();
    snapshot.channels.front().core.frame = std::make_shared<const VnaCore::MeasurementFrame>(Frame(0.2));
    Require(Response(snapshot).Find("sweepProgress") == nullptr, "A repeated completed frame is not progress");
    auto partial = *snapshot.channels.front().core.sweepProgress->frame;
    partial.frameSeq = 2U;
    ReplaceProgress(snapshot, partial, 2U);
    const auto response = Response(snapshot);
    Require(Field(response, "current").boolean &&
                Field(Field(Field(response, "sweepProgress"), "frame"), "frameSeq").string == "2",
        "Current complete frame remains valid while the next frame sweeps");
    partial.frameSeq = 3U;
    ReplaceProgress(snapshot, partial, 2U);
    Require(Response(snapshot).Find("sweepProgress") == nullptr, "Skipped preview identity is rejected");
}

void CheckHidden(const std::vector<std::function<void(VnaService::InstrumentSnapshot &)>> &changes)
{
    for (const auto &change : changes) {
        auto snapshot = Snapshot();
        change(snapshot);
        Require(Response(snapshot).Find("sweepProgress") == nullptr, "Stale, failed or calibration progress is hidden");
    }
}

void StaleProgressIdentityIsHidden()
{
    using SnapshotType = VnaService::InstrumentSnapshot;
    CheckHidden({
        [](SnapshotType &s) {
            ++s.channels.front().core.runId;
        },
        [](SnapshotType &s) {
            ++s.channels.front().core.configRevision;
        },
        [](SnapshotType &s) {
            ++s.channels.front().settings.acquisitionRevision;
        },
        [](SnapshotType &s) {
            s.channels.front().core.channelId = 2U;
        },
    });
}

void UnhealthyAndCalibrationProgressIsHidden()
{
    using SnapshotType = VnaService::InstrumentSnapshot;
    CheckHidden({
        [](SnapshotType &s) {
            s.channels.front().core.state = VnaCore::CoreState::FAULTED;
        },
        [](SnapshotType &s) {
            s.channels.front().core.state = VnaCore::CoreState::READY;
        },
        [](SnapshotType &s) {
            s.channels.front().core.state = VnaCore::CoreState::CLOSED;
        },
        [](SnapshotType &s) {
            s.channels.front().core.error = "Device failed";
        },
        [](SnapshotType &s) {
            s.channels.front().calibration.capturePending = true;
        },
        [](SnapshotType &s) {
            s.channels.front().calibration.collecting = true;
        },
        [](SnapshotType &s) {
            ReplaceProgress(s, Frame(0.6), 0U);
        },
        [](SnapshotType &s) {
            ReplaceProgress(s, Frame(0.6), 4U);
        },
        [](SnapshotType &s) {
            s.channels.front().core.sweepProgress.reset();
        },
    });
}

void InvalidProgressFrameIsHidden()
{
    auto snapshot = Snapshot();
    auto partial = *snapshot.channels.front().core.sweepProgress->frame;
    partial.channelId = 2U;
    ReplaceProgress(snapshot, partial, 2U);
    Require(Response(snapshot).Find("sweepProgress") == nullptr, "Another channel's progress is hidden");
    partial = Frame(0.6);
    partial.frameSeq = 2U;
    ReplaceProgress(snapshot, partial, 2U);
    Require(Response(snapshot).Find("sweepProgress") == nullptr, "First progress cannot skip the first frame sequence");
    partial.frameSeq = 1U;
    partial.plan.frequencyRaw.pop_back();
    ReplaceProgress(snapshot, partial, 2U);
    Require(Response(snapshot).Find("sweepProgress") == nullptr, "Incomplete physical axis is hidden");
    snapshot = Snapshot();
    snapshot.channels.front().settings.points = 5U;
    Require(Response(snapshot).Find("sweepProgress") == nullptr, "Progress must match committed sweep point count");
}

void WrongCalibrationAxisIsHidden()
{
    auto snapshot = Snapshot();
    auto wrong = std::make_shared<VnaCalibration::CalibrationData>(*snapshot.channels.front().correction);
    wrong->frequencyHz.front() = 0.5;
    snapshot.channels.front().correction = wrong;
    Require(Response(snapshot).Find("sweepProgress") == nullptr, "Progress cannot use another calibration axis");
}

void UnmeasuredSuffixCannotLeakFiniteValues()
{
    auto snapshot = Snapshot();
    ReplaceProgress(snapshot, Frame(0.6), 2U);
    const auto response = Response(snapshot);
    const auto &trace = Field(Field(Field(response, "sweepProgress"), "frame"), "traces").array.front();
    Require(!Field(trace, "valid").array.back().boolean &&
                Field(trace, "real").array.back().type == Json::Type::NULL_VALUE &&
                Field(Field(trace, "projection"), "values").array.back().type == Json::Type::NULL_VALUE,
        "Only completed prefix may be exposed, even if a producer leaves finite values after it");
}

void PackedProgressPreservesAllValuesAndNullTail()
{
    const auto snapshot = Snapshot();
    const auto ordinary = Response(snapshot);
    const auto packed = Response(snapshot, true);
    Require(VnaWeb::StringifyJson(packed) == VnaWeb::StringifyJson(VnaWeb::FrameArrayJson::Pack(ordinary)),
        "Packed sidecar must be bit-preserving equivalent to full ordinary arrays including NaN/null tail");
}
}  // 匿名命名空间

int main()
{
    try {
        FirstSlowSweepHasRealCorrectedPrefix();
        ProgressCannotPublishMarkerReadouts();
        CompleteFrameRemainsExactAndIndependent();
        SameRunRequiresNextFrameSequence();
        StaleProgressIdentityIsHidden();
        UnhealthyAndCalibrationProgressIsHidden();
        InvalidProgressFrameIsHidden();
        WrongCalibrationAxisIsHidden();
        UnmeasuredSuffixCannotLeakFiniteValues();
        PackedProgressPreservesAllValuesAndNullTail();
        std::cout
            << "PASS true sweep progress sidecar, corrected prefix, invalid tail, identities, markers and packed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
