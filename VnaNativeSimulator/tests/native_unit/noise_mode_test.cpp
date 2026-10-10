/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <memory>
#include <stdexcept>

#include "native_simulator_state.h"
#include "native_unit_test_fixtures.h"
#include "native_unit_tests.h"
#include "reject_pending_commit.h"

using vna_test::RejectPendingCommit;
#include "scoped_simulator_environment.h"

using vna_native::DRV_SIM_INTERNAL_ERROR;
using vna_native::DRV_SIM_INVALID_ARGUMENT;
using vna_native::DRV_SIM_OK;
using vna_native::NativeSimulatorState;

namespace {

void ConfigureNoiseState(NativeSimulatorState &state, vna_test::EncodedFrame &frame)
{
    vna_test::Expect(state.HwInit() == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetRfPowerOn(1U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.ActivateRxLink(0xFU) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.SetFastSweepMode(0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(
        state.DataFrameDownload(2U, frame.parameters.data(), static_cast<std::uint32_t>(frame.parameters.size()),
            frame.powerTable.data(), static_cast<std::uint32_t>(frame.powerTable.size())) == DRV_SIM_OK,
        vna_test::TestLocation{__FILE__, __LINE__});
}

std::vector<double> AcquireNoiseState(NativeSimulatorState &state)
{
    std::vector<double> output(32U);
    vna_test::Expect(state.StartSweep(2U, 0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(state.GetSweepData(2U, output.data()) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    return output;
}

}  // namespace

static void NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed()
{
    vna_test::ScopedSimulatorEnvironment environment("noise", "42");
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> first = AcquireNoiseState(state);
    const std::vector<double> second = AcquireNoiseState(state);
    vna_test::Expect(first != second, vna_test::TestLocation{__FILE__, __LINE__});
    ConfigureNoiseState(state, frame);
    vna_test::Expect(first == AcquireNoiseState(state), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(second == AcquireNoiseState(state), vna_test::TestLocation{__FILE__, __LINE__});
}

static void NoiseStateFailedStartDoesNotAdvanceSequence()
{
    vna_test::ScopedSimulatorEnvironment environment("noise", "0");
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> expected = AcquireNoiseState(state);
    ConfigureNoiseState(state, frame);
    RejectPendingCommit injector;
    state.SetFailureInjectorForTest(&injector);
    vna_test::Expect(state.StartSweep(2U, 0U) == DRV_SIM_INTERNAL_ERROR, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(!(state.SnapshotForTest().pendingSweep), vna_test::TestLocation{__FILE__, __LINE__});
    state.SetFailureInjectorForTest(nullptr);
    vna_test::Expect(state.StartSweep(2U, 4U) == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(expected == AcquireNoiseState(state), vna_test::TestLocation{__FILE__, __LINE__});
}

static void NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration()
{
    vna_test::ScopedSimulatorEnvironment environment("noise", "18446744073709551615");
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> expected = AcquireNoiseState(state);
    ConfigureNoiseState(state, frame);
    vna_test::Expect(state.StartSweep(2U, 0U) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    const char *invalidSeeds[] = {"-1", "+1", " 1", "0x1", "1x", "18446744073709551616"};
    for (const char *seed : invalidSeeds) {
        environment.Set("noise", seed);
        vna_test::Expect(state.HwInit() == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(state.SnapshotForTest().pendingSweep, vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(state.SnapshotForTest().rfEnabled, vna_test::TestLocation{__FILE__, __LINE__});
    }
    environment.Set("unknown", "1");
    vna_test::Expect(state.HwInit() == DRV_SIM_INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    std::vector<double> actual(32U);
    vna_test::Expect(state.GetSweepData(2U, actual.data()) == DRV_SIM_OK, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(expected == actual, vna_test::TestLocation{__FILE__, __LINE__});
}

namespace vna_test {

void RunNoiseModeTests(TestRunner &runner)
{
    runner.Run(
        "NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed", NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed);
    runner.Run("NoiseStateFailedStartDoesNotAdvanceSequence", NoiseStateFailedStartDoesNotAdvanceSequence);
    runner.Run("NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration",
        NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration);
}

}  // namespace vna_test
