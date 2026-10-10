/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "joined_threads.h"
#include "native_simulator_state.h"
#include "native_unit_test_fixtures.h"
#include "native_unit_tests.h"
#include "throw_at_point.h"

using vna_test::ThrowAtPoint;

using VnaNative::DRV_SIM_INTERNAL_ERROR;
using VnaNative::DRV_SIM_INVALID_ARGUMENT;
using VnaNative::DRV_SIM_INVALID_STATE;
using VnaNative::DRV_SIM_NOT_INITIALIZED;
using VnaNative::DRV_SIM_NOT_SUPPORTED;
using VnaNative::DRV_SIM_OK;
using VnaNative::DRV_SIM_RF_DISABLED;
using VnaNative::FailureInjector;
using VnaNative::FailurePoint;
using VnaNative::NativeSimulatorSnapshot;
using VnaNative::NativeSimulatorState;
using VnaNative::NormalFrameSnapshot;

namespace {

std::uint32_t Download(NativeSimulatorState &state, vna_test::EncodedFrame &frame, std::uint32_t pointCount)
{
    return state.DataFrameDownload(pointCount, frame.parameters.data(),
        static_cast<std::uint32_t>(frame.parameters.size()), frame.powerTable.data(),
        static_cast<std::uint32_t>(frame.powerTable.size()));
}

void Configure(NativeSimulatorState &state)
{
    vna_test::Expect(state.HwInit() == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.ActivateRxLink(0xFU) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
}

}  // namespace

static void NativeStateRequiresInitializationWithoutWritingOutputs()
{
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(1U, 0U, 1000000000ULL);
    double iq[16] = {13.0};
    char idn[16] = "unchanged";
    std::uint32_t checkBitmap = 0xABCDU;
    std::uint16_t index = 42U;

    vna_test::Expect(state.GetSweepData(1U, iq) == DRV_SIM_NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(1U, 0U) == DRV_SIM_NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.ActivateRxLink(1U) == DRV_SIM_NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetRfPowerOn(1U) == DRV_SIM_NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetFastSweepMode(7U) == DRV_SIM_NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.GetIDN(idn, static_cast<int>(sizeof(idn))) == DRV_SIM_NOT_INITIALIZED,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.CheckSweepPortPower(1U, &checkBitmap, &index, 1U) == DRV_SIM_NOT_INITIALIZED,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(Download(state, frame, 1U) == DRV_SIM_NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});

    vna_test::Expect(iq[0] == 13.0, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(idn) == std::string("unchanged"), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(checkBitmap == 0xABCDU, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(index == 42U, vna_test::TestLocation{__FILE__, __LINE__});
}

static void NativeStatePersistsConfigurationAndResetsDeterministically()
{
    NativeSimulatorState state;
    vna_test::Expect(state.HwInit() == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
    vna_test::Expect(snapshot.initialized, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(snapshot.rfEnabled), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(snapshot.rxConfigured), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(snapshot.fastModeConfigured), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(snapshot.pendingSweep), vna_test::TestLocation{__FILE__, __LINE__});

    vna_test::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.ActivateRxLink(0x5U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetFastSweepMode(1U) == DRV_SIM_NOT_SUPPORTED, vna_test::TestLocation{__FILE__, __LINE__});
    snapshot = state.SnapshotForTest();
    vna_test::Expect(snapshot.rfEnabled, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.rxConfigured, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.rxPortBitmap == 0x5U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.fastModeConfigured, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.fastSweepMode == 0U, vna_test::TestLocation{__FILE__, __LINE__});

    vna_test::Expect(state.SetRfPowerOn(2U) == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.ActivateRxLink(0U) == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(
        state.ActivateRxLink(0x10U) == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    snapshot = state.SnapshotForTest();
    vna_test::Expect(snapshot.rfEnabled, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.rxPortBitmap == 0x5U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.fastModeConfigured, vna_test::TestLocation{__FILE__, __LINE__});

    vna_test::Expect(state.HwInit() == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    snapshot = state.SnapshotForTest();
    vna_test::Expect(snapshot.initialized, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(snapshot.rfEnabled), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(snapshot.rxConfigured), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.rxPortBitmap == 0U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(snapshot.fastModeConfigured), vna_test::TestLocation{__FILE__, __LINE__});
    for (std::size_t port = 0U; port < snapshot.frameValid.size(); ++port) {
        vna_test::Expect(!(snapshot.frameValid[port]), vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(snapshot.framePointCount[port] == 0U, vna_test::TestLocation{__FILE__, __LINE__});
    }
}

static void NativeStateOwnsIndependentFramesForEveryPort()
{
    NativeSimulatorState state;
    vna_test::Expect(state.HwInit() == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    std::vector<vna_test::EncodedFrame> inputs;
    for (std::uint32_t port = 0U; port < 4U; ++port) {
        inputs.push_back(vna_test::MakeEncodedFrame(2U, port, 0x100000000ULL * (port + 1U) + 17U));
        inputs.back().powerTable[0U] = std::numeric_limits<double>::quiet_NaN();
        inputs.back().powerTable[1U] = std::numeric_limits<double>::infinity();
        inputs.back().powerTable[2U] = -std::numeric_limits<double>::infinity();
        vna_test::Expect(Download(state, inputs.back(), 2U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    }

    for (std::uint32_t port = 0U; port < 4U; ++port) {
        const std::uint64_t firstFrequency = 0x100000000ULL * (port + 1U) + 17U;
        const std::vector<double> expectedTable = inputs[port].powerTable;
        std::fill(inputs[port].parameters.begin(), inputs[port].parameters.end(), 0U);
        std::fill(inputs[port].powerTable.begin(), inputs[port].powerTable.end(), -1.0);
        std::vector<std::uint32_t>().swap(inputs[port].parameters);
        std::vector<double>().swap(inputs[port].powerTable);

        NormalFrameSnapshot copy;
        vna_test::Expect(state.CopyFrameForTest(port, copy) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(copy.sourcePort == port, vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(copy.points.size() == 2U, vna_test::TestLocation{__FILE__, __LINE__});
        for (std::size_t point = 0U; point < copy.points.size(); ++point) {
            vna_test::Expect(
                copy.points[point].frequencyRaw == firstFrequency + static_cast<std::uint64_t>(point) * 0x100000003ULL,
                vna_test::TestLocation{__FILE__, __LINE__});
            vna_test::Expect(copy.points[point].sourcePort == port, vna_test::TestLocation{__FILE__, __LINE__});
            vna_test::Expect(copy.points[point].ifBandwidthRaw == static_cast<std::uint32_t>(100U + point),
                vna_test::TestLocation{__FILE__, __LINE__});
        }
        vna_test::Expect(copy.tableData.size() == expectedTable.size(), vna_test::TestLocation{__FILE__, __LINE__});
        for (std::size_t index = 0U; index < expectedTable.size(); ++index) {
            if (std::isnan(expectedTable[index])) {
                vna_test::Expect(std::isnan(copy.tableData[index]), vna_test::TestLocation{__FILE__, __LINE__});
            } else {
                vna_test::Expect(
                    copy.tableData[index] == expectedTable[index], vna_test::TestLocation{__FILE__, __LINE__});
            }
        }
    }
}

static void NativeStateEnforcesStartGetAndPendingRules()
{
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 1U, 1000000000ULL);
    vna_test::Expect(state.HwInit() == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(2U, 1U) == DRV_SIM_RF_DISABLED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.ActivateRxLink(0xFU) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(Download(state, frame, 2U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(1U, 1U) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(2U, 4U) == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(2U, 1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});

    NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
    vna_test::Expect(snapshot.pendingSweep, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.pendingPointCount == 2U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(snapshot.pendingSourcePort == 1U, vna_test::TestLocation{__FILE__, __LINE__});

    std::vector<double> output(32U, 9.0);
    vna_test::Expect(
        state.GetSweepData(1U, output.data()) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SnapshotForTest().pendingSweep, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.GetSweepData(2U, output.data()) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(state.SnapshotForTest().pendingSweep), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(
        state.GetSweepData(2U, output.data()) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});

    vna_test::Expect(state.StartSweep(2U, 1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetRfPowerOn(0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(state.SnapshotForTest().pendingSweep), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(
        state.GetSweepData(2U, output.data()) == DRV_SIM_INVALID_STATE, vna_test::TestLocation{__FILE__, __LINE__});
}

static void NativeStateFrameCommitHasStrongExceptionGuarantee()
{
    NativeSimulatorState state;
    vna_test::Expect(state.HwInit() == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::EncodedFrame oldFrame = vna_test::MakeEncodedFrame(1U, 0U, 111ULL);
    vna_test::EncodedFrame replacement = vna_test::MakeEncodedFrame(2U, 0U, 222ULL);
    vna_test::Expect(Download(state, oldFrame, 1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});

    ThrowAtPoint injector(FailurePoint::BEFORE_FRAME_COMMIT);
    state.SetFailureInjectorForTest(&injector);
    vna_test::Expect(
        Download(state, replacement, 2U) == DRV_SIM_INTERNAL_ERROR, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(injector.ThrowCount() == 1U, vna_test::TestLocation{__FILE__, __LINE__});

    NormalFrameSnapshot stored;
    vna_test::Expect(state.CopyFrameForTest(0U, stored) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(stored.points.size() == 1U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(stored.points[0].frequencyRaw == 111U, vna_test::TestLocation{__FILE__, __LINE__});

    state.SetFailureInjectorForTest(nullptr);
    vna_test::Expect(Download(state, replacement, 2U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.CopyFrameForTest(0U, stored) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(stored.points.size() == 2U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(stored.points[0].frequencyRaw == 222U, vna_test::TestLocation{__FILE__, __LINE__});
}

static void NativeStatePendingCommitHasStrongExceptionGuarantee()
{
    NativeSimulatorState state;
    Configure(state);
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(1U, 0U, 333ULL);
    vna_test::Expect(Download(state, frame, 1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});

    ThrowAtPoint injector(FailurePoint::BEFORE_PENDING_COMMIT);
    state.SetFailureInjectorForTest(&injector);
    vna_test::Expect(state.StartSweep(1U, 0U) == DRV_SIM_INTERNAL_ERROR, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(injector.ThrowCount() == 1U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(state.SnapshotForTest().pendingSweep), vna_test::TestLocation{__FILE__, __LINE__});

    state.SetFailureInjectorForTest(nullptr);
    vna_test::Expect(state.StartSweep(1U, 0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    std::vector<double> output(16U, 0.0);
    vna_test::Expect(state.GetSweepData(1U, output.data()) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
}

static void NativeStateSerializesConcurrentConfigurationAndDownloads()
{
    NativeSimulatorState state;
    Configure(state);
    vna_test::EncodedFrame portZero = vna_test::MakeEncodedFrame(2U, 0U, 1000ULL);
    vna_test::EncodedFrame portOne = vna_test::MakeEncodedFrame(2U, 1U, 2000ULL);
    vna_test::Expect(Download(state, portZero, 2U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(Download(state, portOne, 2U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});

    std::atomic<std::uint32_t> failures(0U);
    const std::uint32_t iterations = 500U;
    vna_test::JoinedThreads workers(4U);
    workers.Launch([&state, &portZero, &failures, iterations]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            if (Download(state, portZero, 2U) != DRV_SIM_OK) {
                ++failures;
            }
        }
    });
    workers.Launch([&state, &portOne, &failures, iterations]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            if (Download(state, portOne, 2U) != DRV_SIM_OK) {
                ++failures;
            }
        }
    });
    workers.Launch([&state, &failures, iterations]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            if (state.ActivateRxLink((index % 15U) + 1U) != DRV_SIM_OK || state.SetFastSweepMode(0U) != DRV_SIM_OK ||
                state.SetRfPowerOn(index % 2U) != DRV_SIM_OK) {
                ++failures;
            }
        }
    });
    workers.Launch([&state, &failures, iterations]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            const NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
            if (!snapshot.initialized || !snapshot.frameValid[0U] || !snapshot.frameValid[1U]) {
                ++failures;
            }
        }
    });
    workers.Join();

    vna_test::Expect(failures.load() == 0U, vna_test::TestLocation{__FILE__, __LINE__});
    const NativeSimulatorSnapshot finalSnapshot = state.SnapshotForTest();
    vna_test::Expect(finalSnapshot.frameValid[0U], vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(finalSnapshot.frameValid[1U], vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(finalSnapshot.framePointCount[0U] == 2U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(finalSnapshot.framePointCount[1U] == 2U, vna_test::TestLocation{__FILE__, __LINE__});
}

namespace vna_test {

void RunNativeStateTests(TestRunner &runner)
{
    runner.Run("NativeStateRequiresInitializationWithoutWritingOutputs",
        NativeStateRequiresInitializationWithoutWritingOutputs);
    runner.Run("NativeStatePersistsConfigurationAndResetsDeterministically",
        NativeStatePersistsConfigurationAndResetsDeterministically);
    runner.Run("NativeStateOwnsIndependentFramesForEveryPort", NativeStateOwnsIndependentFramesForEveryPort);
    runner.Run("NativeStateEnforcesStartGetAndPendingRules", NativeStateEnforcesStartGetAndPendingRules);
    runner.Run("NativeStateFrameCommitHasStrongExceptionGuarantee", NativeStateFrameCommitHasStrongExceptionGuarantee);
    runner.Run(
        "NativeStatePendingCommitHasStrongExceptionGuarantee", NativeStatePendingCommitHasStrongExceptionGuarantee);
    runner.Run("NativeStateSerializesConcurrentConfigurationAndDownloads",
        NativeStateSerializesConcurrentConfigurationAndDownloads);
}

}  // namespace vna_test
