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

using VnaTest::ThrowAtPoint;

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

std::uint32_t Download(NativeSimulatorState &state, VnaTest::EncodedFrame &frame, std::uint32_t pointCount)
{
    return state.DataFrameDownload(pointCount, frame.parameters.data(),
        static_cast<std::uint32_t>(frame.parameters.size()), frame.powerTable.data(),
        static_cast<std::uint32_t>(frame.powerTable.size()));
}

void Configure(NativeSimulatorState &state)
{
    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.ActivateRxLink(0xFU) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
}

}  // namespace

static void NativeStateRequiresInitializationWithoutWritingOutputs()
{
    NativeSimulatorState state;
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(1U, 0U, 1000000000ULL);
    double iq[16] = {13.0};
    char idn[16] = "unchanged";
    std::uint32_t checkBitmap = 0xABCDU;
    std::uint16_t index = 42U;

    VnaTest::Expect(state.GetSweepData(1U, iq) == DRV_SIM_NOT_INITIALIZED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(1U, 0U) == DRV_SIM_NOT_INITIALIZED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.ActivateRxLink(1U) == DRV_SIM_NOT_INITIALIZED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetRfPowerOn(1U) == DRV_SIM_NOT_INITIALIZED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetFastSweepMode(7U) == DRV_SIM_NOT_INITIALIZED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.GetIDN(idn, static_cast<int>(sizeof(idn))) == DRV_SIM_NOT_INITIALIZED,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.CheckSweepPortPower(1U, &checkBitmap, &index, 1U) == DRV_SIM_NOT_INITIALIZED,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(Download(state, frame, 1U) == DRV_SIM_NOT_INITIALIZED, VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(iq[0] == 13.0, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(std::string(idn) == std::string("unchanged"), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(checkBitmap == 0xABCDU, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(index == 42U, VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NativeStatePersistsConfigurationAndResetsDeterministically()
{
    NativeSimulatorState state;
    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
    VnaTest::Expect(snapshot.initialized, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(snapshot.rfEnabled), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(snapshot.rxConfigured), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(snapshot.fastModeConfigured), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(snapshot.pendingSweep), VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.ActivateRxLink(0x5U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetFastSweepMode(1U) == DRV_SIM_NOT_SUPPORTED, VnaTest::TestLocation{__FILE__, __LINE__});
    snapshot = state.SnapshotForTest();
    VnaTest::Expect(snapshot.rfEnabled, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.rxConfigured, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.rxPortBitmap == 0x5U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.fastModeConfigured, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.fastSweepMode == 0U, VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(state.SetRfPowerOn(2U) == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.ActivateRxLink(0U) == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.ActivateRxLink(0x10U) == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    snapshot = state.SnapshotForTest();
    VnaTest::Expect(snapshot.rfEnabled, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.rxPortBitmap == 0x5U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.fastModeConfigured, VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    snapshot = state.SnapshotForTest();
    VnaTest::Expect(snapshot.initialized, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(snapshot.rfEnabled), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(snapshot.rxConfigured), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.rxPortBitmap == 0U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(snapshot.fastModeConfigured), VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t port = 0U; port < snapshot.frameValid.size(); ++port) {
        VnaTest::Expect(!(snapshot.frameValid[port]), VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(snapshot.framePointCount[port] == 0U, VnaTest::TestLocation{__FILE__, __LINE__});
    }
}

static void NativeStateOwnsIndependentFramesForEveryPort()
{
    NativeSimulatorState state;
    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    std::vector<VnaTest::EncodedFrame> inputs;
    for (std::uint32_t port = 0U; port < 4U; ++port) {
        inputs.push_back(VnaTest::MakeEncodedFrame(2U, port, 0x100000000ULL * (port + 1U) + 17U));
        inputs.back().powerTable[0U] = std::numeric_limits<double>::quiet_NaN();
        inputs.back().powerTable[1U] = std::numeric_limits<double>::infinity();
        inputs.back().powerTable[2U] = -std::numeric_limits<double>::infinity();
        VnaTest::Expect(Download(state, inputs.back(), 2U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    }

    for (std::uint32_t port = 0U; port < 4U; ++port) {
        const std::uint64_t firstFrequency = 0x100000000ULL * (port + 1U) + 17U;
        const std::vector<double> expectedTable = inputs[port].powerTable;
        std::fill(inputs[port].parameters.begin(), inputs[port].parameters.end(), 0U);
        std::fill(inputs[port].powerTable.begin(), inputs[port].powerTable.end(), -1.0);
        std::vector<std::uint32_t>().swap(inputs[port].parameters);
        std::vector<double>().swap(inputs[port].powerTable);

        NormalFrameSnapshot copy;
        VnaTest::Expect(state.CopyFrameForTest(port, copy) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(copy.sourcePort == port, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(copy.points.size() == 2U, VnaTest::TestLocation{__FILE__, __LINE__});
        for (std::size_t point = 0U; point < copy.points.size(); ++point) {
            VnaTest::Expect(
                copy.points[point].frequencyRaw == firstFrequency + static_cast<std::uint64_t>(point) * 0x100000003ULL,
                VnaTest::TestLocation{__FILE__, __LINE__});
            VnaTest::Expect(copy.points[point].sourcePort == port, VnaTest::TestLocation{__FILE__, __LINE__});
            VnaTest::Expect(copy.points[point].ifBandwidthRaw == static_cast<std::uint32_t>(100U + point),
                VnaTest::TestLocation{__FILE__, __LINE__});
        }
        VnaTest::Expect(copy.tableData.size() == expectedTable.size(), VnaTest::TestLocation{__FILE__, __LINE__});
        for (std::size_t index = 0U; index < expectedTable.size(); ++index) {
            if (std::isnan(expectedTable[index])) {
                VnaTest::Expect(std::isnan(copy.tableData[index]), VnaTest::TestLocation{__FILE__, __LINE__});
            } else {
                VnaTest::Expect(
                    copy.tableData[index] == expectedTable[index], VnaTest::TestLocation{__FILE__, __LINE__});
            }
        }
    }
}

static void NativeStateEnforcesStartGetAndPendingRules()
{
    NativeSimulatorState state;
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(2U, 1U, 1000000000ULL);
    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(2U, 1U) == DRV_SIM_RF_DISABLED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.ActivateRxLink(0xFU) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(Download(state, frame, 2U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(1U, 1U) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(2U, 4U) == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(2U, 1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.StartSweep(2U, 1U) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});

    NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
    VnaTest::Expect(snapshot.pendingSweep, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.pendingPointCount == 2U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(snapshot.pendingSourcePort == 1U, VnaTest::TestLocation{__FILE__, __LINE__});

    std::vector<double> output(32U, 9.0);
    VnaTest::Expect(
        state.GetSweepData(1U, output.data()) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SnapshotForTest().pendingSweep, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.GetSweepData(2U, output.data()) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(state.SnapshotForTest().pendingSweep), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        state.GetSweepData(2U, output.data()) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(state.StartSweep(2U, 1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetRfPowerOn(0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(state.SnapshotForTest().pendingSweep), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        state.GetSweepData(2U, output.data()) == DRV_SIM_INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NativeStateFrameCommitHasStrongExceptionGuarantee()
{
    NativeSimulatorState state;
    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::EncodedFrame oldFrame = VnaTest::MakeEncodedFrame(1U, 0U, 111ULL);
    VnaTest::EncodedFrame replacement = VnaTest::MakeEncodedFrame(2U, 0U, 222ULL);
    VnaTest::Expect(Download(state, oldFrame, 1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});

    ThrowAtPoint injector(FailurePoint::BEFORE_FRAME_COMMIT);
    state.SetFailureInjectorForTest(&injector);
    VnaTest::Expect(
        Download(state, replacement, 2U) == DRV_SIM_INTERNAL_ERROR, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(injector.ThrowCount() == 1U, VnaTest::TestLocation{__FILE__, __LINE__});

    NormalFrameSnapshot stored;
    VnaTest::Expect(state.CopyFrameForTest(0U, stored) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(stored.points.size() == 1U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(stored.points[0].frequencyRaw == 111U, VnaTest::TestLocation{__FILE__, __LINE__});

    state.SetFailureInjectorForTest(nullptr);
    VnaTest::Expect(Download(state, replacement, 2U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.CopyFrameForTest(0U, stored) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(stored.points.size() == 2U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(stored.points[0].frequencyRaw == 222U, VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NativeStatePendingCommitHasStrongExceptionGuarantee()
{
    NativeSimulatorState state;
    Configure(state);
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(1U, 0U, 333ULL);
    VnaTest::Expect(Download(state, frame, 1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});

    ThrowAtPoint injector(FailurePoint::BEFORE_PENDING_COMMIT);
    state.SetFailureInjectorForTest(&injector);
    VnaTest::Expect(state.StartSweep(1U, 0U) == DRV_SIM_INTERNAL_ERROR, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(injector.ThrowCount() == 1U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(state.SnapshotForTest().pendingSweep), VnaTest::TestLocation{__FILE__, __LINE__});

    state.SetFailureInjectorForTest(nullptr);
    VnaTest::Expect(state.StartSweep(1U, 0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    std::vector<double> output(16U, 0.0);
    VnaTest::Expect(state.GetSweepData(1U, output.data()) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NativeStateSerializesConcurrentConfigurationAndDownloads()
{
    NativeSimulatorState state;
    Configure(state);
    VnaTest::EncodedFrame portZero = VnaTest::MakeEncodedFrame(2U, 0U, 1000ULL);
    VnaTest::EncodedFrame portOne = VnaTest::MakeEncodedFrame(2U, 1U, 2000ULL);
    VnaTest::Expect(Download(state, portZero, 2U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(Download(state, portOne, 2U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});

    std::atomic<std::uint32_t> failures(0U);
    const std::uint32_t iterations = 500U;
    VnaTest::JoinedThreads workers(4U);
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

    VnaTest::Expect(failures.load() == 0U, VnaTest::TestLocation{__FILE__, __LINE__});
    const NativeSimulatorSnapshot finalSnapshot = state.SnapshotForTest();
    VnaTest::Expect(finalSnapshot.frameValid[0U], VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(finalSnapshot.frameValid[1U], VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(finalSnapshot.framePointCount[0U] == 2U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(finalSnapshot.framePointCount[1U] == 2U, VnaTest::TestLocation{__FILE__, __LINE__});
}

namespace VnaTest {

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

}  // namespace VnaTest
