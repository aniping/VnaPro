/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

#include "native_simulator_state.h"
#include "native_unit_test_fixtures.h"
#include "native_unit_tests.h"
#include "reject_pending_commit.h"
#include "signal_model.h"

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

}  // 匿名命名空间

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
    const char *invalidAmplitudes[] = {
        "-0.001", "1.001", "nan", "inf", " 0.2", "0.2 ", "0.2x", "0x1", "1e309", "1e-400", "-1e-400", "1e-324"};
    for (const char *amplitude : invalidAmplitudes) {
        environment.Set("noise", "42", amplitude);
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

static void NoiseAmplitudeScalesAndZeroMatchesGolden()
{
    VnaTest::ScopedSimulatorEnvironment environment("golden", "invalid", "invalid");
    NativeSimulatorState state;
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> golden = AcquireNoiseState(state);
    environment.Set("noise", "42");
    ConfigureNoiseState(state, frame);
    const std::vector<double> defaultNoise = AcquireNoiseState(state);
    environment.Set("noise", "42", "");
    ConfigureNoiseState(state, frame);
    VnaTest::Expect(defaultNoise == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
    environment.Set("noise", "42", "2e-3");
    ConfigureNoiseState(state, frame);
    VnaTest::Expect(defaultNoise == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
    environment.Set("noise", "42", "0.004");
    ConfigureNoiseState(state, frame);
    const std::vector<double> amplified = AcquireNoiseState(state);
    for (std::size_t index = 0U; index < golden.size(); ++index) {
        VnaTest::ExpectNear(amplified[index] - golden[index], 2.0 * (defaultNoise[index] - golden[index]), 1.0e-15,
            VnaTest::TestLocation{__FILE__, __LINE__});
    }
    environment.Set("noise", nullptr, "0");
    ConfigureNoiseState(state, frame);
    VnaTest::Expect(golden == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(golden == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
    environment.Set("noise", "42", "0e10");
    ConfigureNoiseState(state, frame);
    VnaTest::Expect(golden == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
    environment.Set("noise", "42", "1e-320");
    VnaTest::Expect(state.HwInit() == DRV_SIM_OK, VnaTest::TestLocation{__FILE__, __LINE__});
    environment.Set("noise", "42", "1");
    ConfigureNoiseState(state, frame);
    const std::vector<double> largest = AcquireNoiseState(state);
    for (std::size_t index = 0U; index < golden.size(); index += 2U) {
        const double delta = std::hypot(largest[index] - golden[index], largest[index + 1U] - golden[index + 1U]);
        VnaTest::Expect(std::isfinite(delta) && delta <= std::hypot(golden[index], golden[index + 1U]) + 1.0e-15,
            VnaTest::TestLocation{__FILE__, __LINE__});
    }
}

static void NoiseAmplitudeIsFrozenAndNonDefaultSeedReplays()
{
    VnaTest::ScopedSimulatorEnvironment environment("noise", "43", "0.01");
    NativeSimulatorState state;
    VnaTest::EncodedFrame frame = VnaTest::MakeEncodedFrame(2U, 0U, 1000000000ULL);
    ConfigureNoiseState(state, frame);
    const std::vector<double> first = AcquireNoiseState(state);
    const std::vector<double> second = AcquireNoiseState(state);
    ConfigureNoiseState(state, frame);
    environment.Set("noise", "44", "0");
    VnaTest::Expect(first == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(second == AcquireNoiseState(state), VnaTest::TestLocation{__FILE__, __LINE__});
}

static void NoiseModelRejectsInvalidAmplitudeWithoutChangingOutput()
{
    const VnaNative::NormalFrameSnapshot frame = VnaTest::MakeSnapshot(0U, std::vector<std::uint64_t>{1000000000ULL});
    std::vector<double> output(3U, 42.0);
    const double invalidAmplitudes[] = {
        -0.001, 1.001, (std::numeric_limits<double>::infinity)(), (std::numeric_limits<double>::quiet_NaN)()};
    for (double amplitude : invalidAmplitudes) {
        VnaTest::Expect(
            VnaNative::SignalModel::GenerateNoisy(frame, 42U, 0U, output, amplitude) == DRV_SIM_INVALID_ARGUMENT,
            VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(output == std::vector<double>(3U, 42.0), VnaTest::TestLocation{__FILE__, __LINE__});
    }
}

namespace VnaTest {

void RunNoiseModeTests(TestRunner &runner)
{
    runner.Run(
        "NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed", NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed);
    runner.Run("NoiseStateFailedStartDoesNotAdvanceSequence", NoiseStateFailedStartDoesNotAdvanceSequence);
    runner.Run("NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration",
        NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration);
    runner.Run("NoiseAmplitudeScalesAndZeroMatchesGolden", NoiseAmplitudeScalesAndZeroMatchesGolden);
    runner.Run("NoiseAmplitudeIsFrozenAndNonDefaultSeedReplays", NoiseAmplitudeIsFrozenAndNonDefaultSeedReplays);
    runner.Run("NoiseModelRejectsInvalidAmplitudeWithoutChangingOutput",
        NoiseModelRejectsInvalidAmplitudeWithoutChangingOutput);
}

}  // 命名空间 VnaTest
