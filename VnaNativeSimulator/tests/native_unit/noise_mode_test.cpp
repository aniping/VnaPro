#include <memory>
#include <stdexcept>

#include "native_simulator_state.h"
#include "native_unit_test_fixtures.h"
#include "simulator_environment.h"

using vna_native::DRV_SIM_INTERNAL_ERROR;
using vna_native::DRV_SIM_INVALID_ARGUMENT;
using vna_native::DRV_SIM_OK;
using vna_native::NativeSimulatorState;

namespace {

void ConfigureNoiseState(NativeSimulatorState* state, vna_test::EncodedFrame* frame)
{
    VNA_EXPECT_EQ(DRV_SIM_OK, state->HwInit());
    VNA_EXPECT_EQ(DRV_SIM_OK, state->SetRfPowerOn(1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state->ActivateRxLink(0xFU));
    VNA_EXPECT_EQ(DRV_SIM_OK, state->SetFastSweepMode(0U));
    VNA_EXPECT_EQ(DRV_SIM_OK,
        state->DataFrameDownload(2U, frame->parameters.data(), static_cast<std::uint32_t>(frame->parameters.size()),
            frame->powerTable.data(), static_cast<std::uint32_t>(frame->powerTable.size())));
}

std::vector<double> AcquireNoiseState(NativeSimulatorState* state)
{
    std::vector<double> output(32U);
    VNA_EXPECT_EQ(DRV_SIM_OK, state->StartSweep(2U, 0U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state->GetSweepData(2U, output.data()));
    return output;
}

class RejectPendingCommit : public vna_native::FailureInjector {
public:
    void ThrowIfRequested(vna_native::FailurePoint point) override
    {
        if (point == vna_native::FailurePoint::kBeforePendingCommit) {
            throw std::runtime_error("噪声扫描提交失败");
        }
    }
};

}  // namespace

VNA_TEST(NoiseStateVariesSuccessfulSweepsAndResetReplaysSeed)
{
    vna_test::ScopedSimulatorEnvironment environment("noise", "42");
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 0U, UINT64_C(1000000000));
    ConfigureNoiseState(&state, &frame);
    const std::vector<double> first = AcquireNoiseState(&state);
    const std::vector<double> second = AcquireNoiseState(&state);
    VNA_EXPECT_TRUE(first != second);
    ConfigureNoiseState(&state, &frame);
    VNA_EXPECT_TRUE(first == AcquireNoiseState(&state));
    VNA_EXPECT_TRUE(second == AcquireNoiseState(&state));
}

VNA_TEST(NoiseStateFailedStartDoesNotAdvanceSequence)
{
    vna_test::ScopedSimulatorEnvironment environment("noise", "0");
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 0U, UINT64_C(1000000000));
    ConfigureNoiseState(&state, &frame);
    const std::vector<double> expected = AcquireNoiseState(&state);
    ConfigureNoiseState(&state, &frame);
    state.SetFailureInjectorForTest(std::make_shared<RejectPendingCommit>());
    VNA_EXPECT_EQ(DRV_SIM_INTERNAL_ERROR, state.StartSweep(2U, 0U));
    VNA_EXPECT_FALSE(state.SnapshotForTest().pendingSweep);
    state.SetFailureInjectorForTest(std::shared_ptr<vna_native::FailureInjector>());
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT, state.StartSweep(2U, 4U));
    VNA_EXPECT_TRUE(expected == AcquireNoiseState(&state));
}

VNA_TEST(NoiseStateInvalidEnvironmentPreservesPendingAndConfiguration)
{
    vna_test::ScopedSimulatorEnvironment environment("noise", "18446744073709551615");
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 0U, UINT64_C(1000000000));
    ConfigureNoiseState(&state, &frame);
    const std::vector<double> expected = AcquireNoiseState(&state);
    ConfigureNoiseState(&state, &frame);
    VNA_EXPECT_EQ(DRV_SIM_OK, state.StartSweep(2U, 0U));
    const char* invalidSeeds[] = {"-1", "+1", " 1", "0x1", "1x", "18446744073709551616"};
    for (const char* seed : invalidSeeds) {
        environment.Set("noise", seed);
        VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT, state.HwInit());
        VNA_EXPECT_TRUE(state.SnapshotForTest().pendingSweep);
        VNA_EXPECT_TRUE(state.SnapshotForTest().rfEnabled);
    }
    environment.Set("unknown", "1");
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT, state.HwInit());
    std::vector<double> actual(32U);
    VNA_EXPECT_EQ(DRV_SIM_OK, state.GetSweepData(2U, actual.data()));
    VNA_EXPECT_TRUE(expected == actual);
}
