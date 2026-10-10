/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <memory>
#include <stdexcept>

#include "native_simulator_state.h"
#include "native_unit_test_fixtures.h"
#include "native_unit_tests.h"
#include "reject_pending_commit.h"

using VnaTest::RejectPendingCommit;
#include "scoped_simulator_environment.h"

using VnaNative::DRV_SIM_INTERNAL_ERROR;
using VnaNative::DRV_SIM_INVALID_ARGUMENT;
using VnaNative::DRV_SIM_OK;
using VnaNative::NativeSimulatorState;

namespace {

void ConfigureNoiseState(NativeSimulatorState &state, VnaTest::EncodedFrame &frame)
{
    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.ActivateRxLink(0xFU) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        state.DataFrameDownload(2U, frame.parameters.data(), static_cast<std::uint32_t>(frame.parameters.size()),
            frame.powerTable.data(), static_cast<std::uint32_t>(frame.powerTable.size())) == DRV_SIM_OK,
        VnaTest::TestLocation{__FILE__, __LINE__});
}

std::vector<double> AcquireNoiseState(NativeSimulatorState &state)
{
    std::vector<double> output(32U);
    VnaTest::Expect(state.StartSweep(2U, 0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(state.GetSweepData(2U, output.data()) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    return output;
}

}  // namespace

static void NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed()
{
    VnaTest::ScopedSimulatorEnvironment environment("noise", "42");
    NativeSimulatorState state;
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> first = AcquireNoiseState(state);
    const std::vector<double> second = AcquireNoiseState(state);
    VnaTest::Expect(first != second, VnaTest::TestLocation{__FILE__, __LINE__});
    ConfigureNoiseState(state, frame);
    VnaTest::Expect(first == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(second == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NoiseStateFailedStartDoesNotAdvanceSequence()
{
    VnaTest::ScopedSimulatorEnvironment environment("noise", "0");
    NativeSimulatorState state;
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> expected = AcquireNoiseState(state);
    ConfigureNoiseState(state, frame);
    RejectPendingCommit injector;
    state.SetFailureInjectorForTest(&injector);
    VnaTest::Expect(state.StartSweep(2U, 0U) == DRV_SIM_INTERNAL_ERROR, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(!(state.SnapshotForTest().pendingSweep), VnaTest::TestLocation{__FILE__, __LINE__});
    state.SetFailureInjectorForTest(nullptr);
    VnaTest::Expect(state.StartSweep(2U, 4U) == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(expected == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration()
{
    VnaTest::ScopedSimulatorEnvironment environment("noise", "18446744073709551615");
    NativeSimulatorState state;
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> expected = AcquireNoiseState(state);
    ConfigureNoiseState(state, frame);
    VnaTest::Expect(state.StartSweep(2U, 0U) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    const char *invalidSeeds[] = {"-1", "+1", " 1", "0x1", "1x", "18446744073709551616"};
    for (const char *seed : invalidSeeds) {
        environment.Set("noise", seed);
        VnaTest::Expect(state.HwInit() == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(state.SnapshotForTest().pendingSweep, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(state.SnapshotForTest().rfEnabled, VnaTest::TestLocation{__FILE__, __LINE__});
    }
    environment.Set("unknown", "1");
    VnaTest::Expect(state.HwInit() == DRV_SIM_INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    std::vector<double> actual(32U);
    VnaTest::Expect(state.GetSweepData(2U, actual.data()) == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(expected == actual, VnaTest::TestLocation{__FILE__, __LINE__});
}

namespace VnaTest {

void RunNoiseModeTests(TestRunner &runner)
{
    runner.Run(
        "NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed", NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed);
    runner.Run("NoiseStateFailedStartDoesNotAdvanceSequence", NoiseStateFailedStartDoesNotAdvanceSequence);
    runner.Run("NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration",
        NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration);
}

}  // namespace VnaTest
