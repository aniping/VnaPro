#include "native_contract_test_support.h"
#include "simulator_environment.h"

namespace vna_test {
namespace {

void ConfigureNoiseSweep(const NativeFunctions& functions, FrameInput* frame)
{
    VNA_EXPECT_EQ(kOk, functions.hwInit());
    VNA_EXPECT_EQ(kOk, functions.setRfPowerOn(1U));
    VNA_EXPECT_EQ(kOk, functions.activateRxLink(0xFU));
    VNA_EXPECT_EQ(kOk, functions.setFastSweepMode(0U));
    VNA_EXPECT_EQ(kOk, DownloadFrame(functions, frame));
}

std::vector<double> Acquire(const NativeFunctions& functions, std::uint32_t sourcePort)
{
    VNA_EXPECT_EQ(kOk, functions.startSweep(3U, sourcePort));
    std::vector<double> output(48U);
    VNA_EXPECT_EQ(kOk, functions.getSweepData(3U, output.data()));
    return output;
}

void ExpectBoundedNoise(const std::vector<double>& golden, const std::vector<double>& noisy)
{
    VNA_EXPECT_EQ(golden.size(), noisy.size());
    for (std::size_t index = 0U; index < golden.size(); index += 2U) {
        VNA_EXPECT_TRUE(std::isfinite(noisy[index]) && std::isfinite(noisy[index + 1U]));
        const double delta = std::hypot(noisy[index] - golden[index], noisy[index + 1U] - golden[index + 1U]);
        VNA_EXPECT_TRUE(delta > 0.0);
        VNA_EXPECT_TRUE(delta <= 0.002 * std::hypot(golden[index], golden[index + 1U]) + 1.0e-15);
    }
}

}  // namespace

void RunNativeNoiseModeTests(TestRunner* runner, NativeLibraryProbe* library)
{
    const NativeFunctions& functions = library->Functions();
    runner->Run("噪声保持四端口映射、有限值与 0.2% 复数幅度边界，种子及复位可复现", [&functions]() {
        ScopedSimulatorEnvironment environment("noise", "42");
        for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
            FrameInput frame = MakeNormalFrame(3U, sourcePort, UINT64_C(0x123456789abcdef0));
            ConfigureNoiseSweep(functions, &frame);
            const std::vector<double> first = Acquire(functions, sourcePort);
            const std::vector<double> second = Acquire(functions, sourcePort);
            ExpectBoundedNoise(ExpectedIq(frame.frequencies, sourcePort), first);
            ExpectBoundedNoise(ExpectedIq(frame.frequencies, sourcePort), second);
            VNA_EXPECT_TRUE(first != second);
            ConfigureNoiseSweep(functions, &frame);
            VNA_EXPECT_TRUE(first == Acquire(functions, sourcePort));
            VNA_EXPECT_TRUE(second == Acquire(functions, sourcePort));
            environment.Set("noise", "43");
            ConfigureNoiseSweep(functions, &frame);
            VNA_EXPECT_TRUE(first != Acquire(functions, sourcePort));
            environment.Set("noise", "42");
        }
    });

    runner->Run("噪声模式在 Init 冻结，失败 Get 不消费、pending 不被下载改变且仅能成功 Get 一次", [&functions]() {
        ScopedSimulatorEnvironment environment("noise", NULL);
        FrameInput frame = MakeNormalFrame(3U, 0U, UINT64_C(1000000000));
        ConfigureNoiseSweep(functions, &frame);
        const std::vector<double> first = Acquire(functions, 0U);
        const std::vector<double> second = Acquire(functions, 0U);
        ConfigureNoiseSweep(functions, &frame);
        environment.Set("golden", "invalid-but-ignored-in-golden");
        VNA_EXPECT_EQ(kOk, functions.startSweep(3U, 0U));
        VNA_EXPECT_EQ(kInvalidState, functions.startSweep(3U, 0U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.getSweepData(3U, NULL));
        std::vector<double> storage(50U, -99.0);
        VNA_EXPECT_EQ(kInvalidState, functions.getSweepData(2U, storage.data() + 1U));
        for (double value : storage) {
            VNA_EXPECT_EQ(-99.0, value);
        }
        FrameInput replacement = MakeNormalFrame(3U, 0U, UINT64_C(2000000000));
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &replacement));
        VNA_EXPECT_EQ(kOk, functions.getSweepData(3U, storage.data() + 1U));
        VNA_EXPECT_EQ(-99.0, storage.front());
        VNA_EXPECT_EQ(-99.0, storage.back());
        VNA_EXPECT_TRUE(first == std::vector<double>(storage.begin() + 1U, storage.end() - 1U));
        VNA_EXPECT_EQ(kInvalidState, functions.getSweepData(3U, storage.data() + 1U));
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));
        VNA_EXPECT_TRUE(second == Acquire(functions, 0U));
        ConfigureNoiseSweep(functions, &frame);
        ExpectIqNear(ExpectedIq(frame.frequencies, 0U), Acquire(functions, 0U).data());
        environment.Set("noise", "1");
        ConfigureNoiseSweep(functions, &frame);
        VNA_EXPECT_EQ(kOk, functions.startSweep(3U, 0U));
        VNA_EXPECT_EQ(kOk, functions.setRfPowerOn(0U));
        VNA_EXPECT_EQ(kInvalidState, functions.getSweepData(3U, storage.data() + 1U));
        VNA_EXPECT_EQ(kOk, functions.setRfPowerOn(1U));
        VNA_EXPECT_TRUE(second == Acquire(functions, 0U));
    });
}

}  // namespace vna_test
