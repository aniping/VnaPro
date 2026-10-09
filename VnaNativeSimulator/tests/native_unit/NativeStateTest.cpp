#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "NativeSimulatorState.h"
#include "NativeUnitTestFixtures.h"

using vna_native::DRV_SIM_INTERNAL_ERROR;
using vna_native::DRV_SIM_INVALID_ARGUMENT;
using vna_native::DRV_SIM_INVALID_STATE;
using vna_native::DRV_SIM_NOT_INITIALIZED;
using vna_native::DRV_SIM_NOT_SUPPORTED;
using vna_native::DRV_SIM_OK;
using vna_native::DRV_SIM_RF_DISABLED;
using vna_native::FailureInjector;
using vna_native::FailurePoint;
using vna_native::NativeSimulatorSnapshot;
using vna_native::NativeSimulatorState;
using vna_native::NormalFrameSnapshot;

namespace {

std::uint32_t Download(NativeSimulatorState* state, vna_test::EncodedFrame* frame, std::uint32_t pointCount)
{
    return state->DataFrameDownload(pointCount, frame->parameters.data(),
        static_cast<std::uint32_t>(frame->parameters.size()), frame->powerTable.data(),
        static_cast<std::uint32_t>(frame->powerTable.size()));
}

void Configure(NativeSimulatorState* state)
{
    VNA_EXPECT_EQ(DRV_SIM_OK, state->HwInit());
    VNA_EXPECT_EQ(DRV_SIM_OK, state->SetRfPowerOn(1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state->ActivateRxLink(0xFU));
    VNA_EXPECT_EQ(DRV_SIM_OK, state->SetFastSweepMode(0U));
}

class ThrowAtPoint : public FailureInjector {
public:
    explicit ThrowAtPoint(FailurePoint point) : point_(point), throwCount_(0U) {}

    void ThrowIfRequested(FailurePoint point) override
    {
        if (point == point_) {
            ++throwCount_;
            throw std::runtime_error("单元测试故障注入");
        }
    }

    std::uint32_t ThrowCount() const { return throwCount_; }

private:
    FailurePoint point_;
    std::uint32_t throwCount_;
};

}  // namespace

VNA_TEST(NativeStateRequiresInitializationWithoutWritingOutputs)
{
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(1U, 0U, UINT64_C(1000000000));
    double iq[16] = {13.0};
    char idn[16] = "unchanged";
    std::uint32_t checkBitmap = 0xABCDU;
    std::uint16_t index = 42U;

    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, state.GetSweepData(1U, iq));
    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, state.StartSweep(1U, 0U));
    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, state.ActivateRxLink(1U));
    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, state.SetRfPowerOn(1U));
    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, state.SetFastSweepMode(7U));
    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, state.GetIDN(idn, static_cast<int>(sizeof(idn))));
    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, state.CheckSweepPortPower(1U, &checkBitmap, &index, 1U));
    VNA_EXPECT_EQ(DRV_SIM_NOT_INITIALIZED, Download(&state, &frame, 1U));

    VNA_EXPECT_EQ(13.0, iq[0]);
    VNA_EXPECT_EQ(std::string("unchanged"), std::string(idn));
    VNA_EXPECT_EQ(0xABCDU, checkBitmap);
    VNA_EXPECT_EQ(42U, index);
}

VNA_TEST(NativeStatePersistsConfigurationAndResetsDeterministically)
{
    NativeSimulatorState state;
    VNA_EXPECT_EQ(DRV_SIM_OK, state.HwInit());
    NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
    VNA_EXPECT_TRUE(snapshot.initialized);
    VNA_EXPECT_FALSE(snapshot.rfEnabled);
    VNA_EXPECT_FALSE(snapshot.rxConfigured);
    VNA_EXPECT_FALSE(snapshot.fastModeConfigured);
    VNA_EXPECT_FALSE(snapshot.pendingSweep);

    VNA_EXPECT_EQ(DRV_SIM_OK, state.SetRfPowerOn(1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.ActivateRxLink(0x5U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.SetFastSweepMode(0U));
    VNA_EXPECT_EQ(DRV_SIM_NOT_SUPPORTED, state.SetFastSweepMode(1U));
    snapshot = state.SnapshotForTest();
    VNA_EXPECT_TRUE(snapshot.rfEnabled);
    VNA_EXPECT_TRUE(snapshot.rxConfigured);
    VNA_EXPECT_EQ(0x5U, snapshot.rxPortBitmap);
    VNA_EXPECT_TRUE(snapshot.fastModeConfigured);
    VNA_EXPECT_EQ(0U, snapshot.fastSweepMode);

    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT, state.SetRfPowerOn(2U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT, state.ActivateRxLink(0U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT, state.ActivateRxLink(0x10U));
    snapshot = state.SnapshotForTest();
    VNA_EXPECT_TRUE(snapshot.rfEnabled);
    VNA_EXPECT_EQ(0x5U, snapshot.rxPortBitmap);
    VNA_EXPECT_TRUE(snapshot.fastModeConfigured);

    VNA_EXPECT_EQ(DRV_SIM_OK, state.HwInit());
    snapshot = state.SnapshotForTest();
    VNA_EXPECT_TRUE(snapshot.initialized);
    VNA_EXPECT_FALSE(snapshot.rfEnabled);
    VNA_EXPECT_FALSE(snapshot.rxConfigured);
    VNA_EXPECT_EQ(0U, snapshot.rxPortBitmap);
    VNA_EXPECT_FALSE(snapshot.fastModeConfigured);
    for (std::size_t port = 0U; port < snapshot.frameValid.size(); ++port) {
        VNA_EXPECT_FALSE(snapshot.frameValid[port]);
        VNA_EXPECT_EQ(0U, snapshot.framePointCount[port]);
    }
}

VNA_TEST(NativeStateOwnsIndependentFramesForEveryPort)
{
    NativeSimulatorState state;
    VNA_EXPECT_EQ(DRV_SIM_OK, state.HwInit());
    std::vector<vna_test::EncodedFrame> inputs;
    for (std::uint32_t port = 0U; port < 4U; ++port) {
        inputs.push_back(vna_test::MakeEncodedFrame(2U, port, UINT64_C(0x100000000) * (port + 1U) + 17U));
        inputs.back().powerTable[0U] = std::numeric_limits<double>::quiet_NaN();
        inputs.back().powerTable[1U] = std::numeric_limits<double>::infinity();
        inputs.back().powerTable[2U] = -std::numeric_limits<double>::infinity();
        VNA_EXPECT_EQ(DRV_SIM_OK, Download(&state, &inputs.back(), 2U));
    }

    for (std::uint32_t port = 0U; port < 4U; ++port) {
        const std::uint64_t firstFrequency = UINT64_C(0x100000000) * (port + 1U) + 17U;
        const std::vector<double> expectedTable = inputs[port].powerTable;
        std::fill(inputs[port].parameters.begin(), inputs[port].parameters.end(), 0U);
        std::fill(inputs[port].powerTable.begin(), inputs[port].powerTable.end(), -1.0);
        std::vector<std::uint32_t>().swap(inputs[port].parameters);
        std::vector<double>().swap(inputs[port].powerTable);

        NormalFrameSnapshot copy;
        VNA_EXPECT_EQ(DRV_SIM_OK, state.CopyFrameForTest(port, &copy));
        VNA_EXPECT_EQ(port, copy.sourcePort);
        VNA_EXPECT_EQ(2U, copy.points.size());
        for (std::size_t point = 0U; point < copy.points.size(); ++point) {
            VNA_EXPECT_EQ(firstFrequency + static_cast<std::uint64_t>(point) * UINT64_C(0x100000003),
                copy.points[point].frequencyRaw);
            VNA_EXPECT_EQ(port, copy.points[point].sourcePort);
            VNA_EXPECT_EQ(static_cast<std::uint32_t>(100U + point), copy.points[point].ifBandwidthRaw);
        }
        VNA_EXPECT_EQ(expectedTable.size(), copy.tableData.size());
        for (std::size_t index = 0U; index < expectedTable.size(); ++index) {
            if (std::isnan(expectedTable[index])) {
                VNA_EXPECT_TRUE(std::isnan(copy.tableData[index]));
            } else {
                VNA_EXPECT_EQ(expectedTable[index], copy.tableData[index]);
            }
        }
    }
}

VNA_TEST(NativeStateEnforcesStartGetAndPendingRules)
{
    NativeSimulatorState state;
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(2U, 1U, UINT64_C(1000000000));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.HwInit());
    VNA_EXPECT_EQ(DRV_SIM_RF_DISABLED, state.StartSweep(2U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.SetRfPowerOn(1U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.StartSweep(2U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.ActivateRxLink(0xFU));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.StartSweep(2U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.SetFastSweepMode(0U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.StartSweep(2U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, Download(&state, &frame, 2U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.StartSweep(1U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_ARGUMENT, state.StartSweep(2U, 4U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.StartSweep(2U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.StartSweep(2U, 1U));

    NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
    VNA_EXPECT_TRUE(snapshot.pendingSweep);
    VNA_EXPECT_EQ(2U, snapshot.pendingPointCount);
    VNA_EXPECT_EQ(1U, snapshot.pendingSourcePort);

    std::vector<double> output(32U, 9.0);
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.GetSweepData(1U, output.data()));
    VNA_EXPECT_TRUE(state.SnapshotForTest().pendingSweep);
    VNA_EXPECT_EQ(DRV_SIM_OK, state.GetSweepData(2U, output.data()));
    VNA_EXPECT_FALSE(state.SnapshotForTest().pendingSweep);
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.GetSweepData(2U, output.data()));

    VNA_EXPECT_EQ(DRV_SIM_OK, state.StartSweep(2U, 1U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.SetRfPowerOn(0U));
    VNA_EXPECT_FALSE(state.SnapshotForTest().pendingSweep);
    VNA_EXPECT_EQ(DRV_SIM_INVALID_STATE, state.GetSweepData(2U, output.data()));
}

VNA_TEST(NativeStateFrameCommitHasStrongExceptionGuarantee)
{
    NativeSimulatorState state;
    VNA_EXPECT_EQ(DRV_SIM_OK, state.HwInit());
    vna_test::EncodedFrame oldFrame = vna_test::MakeEncodedFrame(1U, 0U, UINT64_C(111));
    vna_test::EncodedFrame replacement = vna_test::MakeEncodedFrame(2U, 0U, UINT64_C(222));
    VNA_EXPECT_EQ(DRV_SIM_OK, Download(&state, &oldFrame, 1U));

    std::shared_ptr<ThrowAtPoint> injector(new ThrowAtPoint(FailurePoint::kBeforeFrameCommit));
    state.SetFailureInjectorForTest(injector);
    VNA_EXPECT_EQ(DRV_SIM_INTERNAL_ERROR, Download(&state, &replacement, 2U));
    VNA_EXPECT_EQ(1U, injector->ThrowCount());

    NormalFrameSnapshot stored;
    VNA_EXPECT_EQ(DRV_SIM_OK, state.CopyFrameForTest(0U, &stored));
    VNA_EXPECT_EQ(1U, stored.points.size());
    VNA_EXPECT_EQ(111U, stored.points[0].frequencyRaw);

    state.SetFailureInjectorForTest(std::shared_ptr<FailureInjector>());
    VNA_EXPECT_EQ(DRV_SIM_OK, Download(&state, &replacement, 2U));
    VNA_EXPECT_EQ(DRV_SIM_OK, state.CopyFrameForTest(0U, &stored));
    VNA_EXPECT_EQ(2U, stored.points.size());
    VNA_EXPECT_EQ(222U, stored.points[0].frequencyRaw);
}

VNA_TEST(NativeStatePendingCommitHasStrongExceptionGuarantee)
{
    NativeSimulatorState state;
    Configure(&state);
    vna_test::EncodedFrame frame = vna_test::MakeEncodedFrame(1U, 0U, UINT64_C(333));
    VNA_EXPECT_EQ(DRV_SIM_OK, Download(&state, &frame, 1U));

    std::shared_ptr<ThrowAtPoint> injector(new ThrowAtPoint(FailurePoint::kBeforePendingCommit));
    state.SetFailureInjectorForTest(injector);
    VNA_EXPECT_EQ(DRV_SIM_INTERNAL_ERROR, state.StartSweep(1U, 0U));
    VNA_EXPECT_EQ(1U, injector->ThrowCount());
    VNA_EXPECT_FALSE(state.SnapshotForTest().pendingSweep);

    state.SetFailureInjectorForTest(std::shared_ptr<FailureInjector>());
    VNA_EXPECT_EQ(DRV_SIM_OK, state.StartSweep(1U, 0U));
    std::vector<double> output(16U, 0.0);
    VNA_EXPECT_EQ(DRV_SIM_OK, state.GetSweepData(1U, output.data()));
}

VNA_TEST(NativeStateSerializesConcurrentConfigurationAndDownloads)
{
    NativeSimulatorState state;
    Configure(&state);
    vna_test::EncodedFrame portZero = vna_test::MakeEncodedFrame(2U, 0U, UINT64_C(1000));
    vna_test::EncodedFrame portOne = vna_test::MakeEncodedFrame(2U, 1U, UINT64_C(2000));
    VNA_EXPECT_EQ(DRV_SIM_OK, Download(&state, &portZero, 2U));
    VNA_EXPECT_EQ(DRV_SIM_OK, Download(&state, &portOne, 2U));

    std::atomic<std::uint32_t> failures(0U);
    const std::uint32_t iterations = 500U;
    std::thread first([&state, &portZero, &failures]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            if (Download(&state, &portZero, 2U) != DRV_SIM_OK) {
                ++failures;
            }
        }
    });
    std::thread second([&state, &portOne, &failures]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            if (Download(&state, &portOne, 2U) != DRV_SIM_OK) {
                ++failures;
            }
        }
    });
    std::thread third([&state, &failures]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            if (state.ActivateRxLink((index % 15U) + 1U) != DRV_SIM_OK || state.SetFastSweepMode(0U) != DRV_SIM_OK ||
                state.SetRfPowerOn(index % 2U) != DRV_SIM_OK) {
                ++failures;
            }
        }
    });
    std::thread fourth([&state, &failures]() {
        for (std::uint32_t index = 0U; index < iterations; ++index) {
            const NativeSimulatorSnapshot snapshot = state.SnapshotForTest();
            if (!snapshot.initialized || !snapshot.frameValid[0U] || !snapshot.frameValid[1U]) {
                ++failures;
            }
        }
    });
    first.join();
    second.join();
    third.join();
    fourth.join();

    VNA_EXPECT_EQ(0U, failures.load());
    const NativeSimulatorSnapshot finalSnapshot = state.SnapshotForTest();
    VNA_EXPECT_TRUE(finalSnapshot.frameValid[0U]);
    VNA_EXPECT_TRUE(finalSnapshot.frameValid[1U]);
    VNA_EXPECT_EQ(2U, finalSnapshot.framePointCount[0U]);
    VNA_EXPECT_EQ(2U, finalSnapshot.framePointCount[1U]);
}
