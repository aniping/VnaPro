/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_contract_test_support.h"
#include "scoped_simulator_environment.h"

namespace VnaTest {
namespace {

void ConfigureNoiseSweep(const NativeFunctions &functions, FrameInput &frame)
{
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.activateRxLink(0xFU) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setFastSweepMode(0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
}

std::vector<double> Acquire(const NativeFunctions &functions, std::uint32_t sourcePort)
{
    VnaTest::Expect(functions.startSweep(3U, sourcePort) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    std::vector<double> output(48U);
    VnaTest::Expect(functions.getSweepData(3U, output.data()) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    return output;
}

void ExpectBoundedNoise(const std::vector<double> &golden, const std::vector<double> &noisy)
{
    VnaTest::Expect(noisy.size() == golden.size(), VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < golden.size(); index += 2U) {
        VnaTest::Expect(
            std::isfinite(noisy[index]) && std::isfinite(noisy[index + 1U]), VnaTest::TestLocation{__FILE__, __LINE__});
        const double delta = std::hypot(noisy[index] - golden[index], noisy[index + 1U] - golden[index + 1U]);
        VnaTest::Expect(delta > 0.0, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(delta <= 0.002 * std::hypot(golden[index], golden[index + 1U]) + 1.0e-15,
            VnaTest::TestLocation{__FILE__, __LINE__});
    }
}

void BoundedRepeatableNoise(const NativeFunctions &functions)
{
    ScopedSimulatorEnvironment environment("noise", "42");
    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        FrameInput frame = MakeNormalFrame(3U, sourcePort, 0x123456789abcdef0ULL);
        ConfigureNoiseSweep(functions, frame);
        const std::vector<double> first = Acquire(functions, sourcePort);
        const std::vector<double> second = Acquire(functions, sourcePort);
        ExpectBoundedNoise(ExpectedIq(frame.frequencies, sourcePort), first);
        ExpectBoundedNoise(ExpectedIq(frame.frequencies, sourcePort), second);
        VnaTest::Expect(first != second, VnaTest::TestLocation{__FILE__, __LINE__});
        ConfigureNoiseSweep(functions, frame);
        VnaTest::Expect(first == Acquire(functions, sourcePort), VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(second == Acquire(functions, sourcePort), VnaTest::TestLocation{__FILE__, __LINE__});
        environment.Set("noise", "43");
        ConfigureNoiseSweep(functions, frame);
        VnaTest::Expect(first != Acquire(functions, sourcePort), VnaTest::TestLocation{__FILE__, __LINE__});
        environment.Set("noise", "42");
    }
}

void FrozenModeAndPendingConsumption(const NativeFunctions &functions)
{
    ScopedSimulatorEnvironment environment("noise", nullptr);
    FrameInput frame = MakeNormalFrame(3U, 0U, 1000000000ULL);
    ConfigureNoiseSweep(functions, frame);
    const std::vector<double> first = Acquire(functions, 0U);
    const std::vector<double> second = Acquire(functions, 0U);
    ConfigureNoiseSweep(functions, frame);
    environment.Set("golden", "invalid-but-ignored-in-golden");
    VnaTest::Expect(functions.startSweep(3U, 0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(3U, 0U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.getSweepData(3U, nullptr) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    std::vector<double> storage(50U, -99.0);
    VnaTest::Expect(
        functions.getSweepData(2U, storage.data() + 1U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    for (double value : storage) {
        VnaTest::Expect(value == -99.0, VnaTest::TestLocation{__FILE__, __LINE__});
    }
    FrameInput replacement = MakeNormalFrame(3U, 0U, 2000000000ULL);
    VnaTest::Expect(DownloadFrame(functions, replacement) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.getSweepData(3U, storage.data() + 1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(storage.front() == -99.0, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(storage.back() == -99.0, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(first == std::vector<double>(storage.begin() + 1U, storage.end() - 1U),
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        functions.getSweepData(3U, storage.data() + 1U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(second == Acquire(functions, 0U), VnaTest::TestLocation{__FILE__, __LINE__});
    ConfigureNoiseSweep(functions, frame);
    ExpectIqNear(ExpectedIq(frame.frequencies, 0U), Acquire(functions, 0U).data());
    environment.Set("noise", "1");
    ConfigureNoiseSweep(functions, frame);
    VnaTest::Expect(functions.startSweep(3U, 0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        functions.getSweepData(3U, storage.data() + 1U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(second == Acquire(functions, 0U), VnaTest::TestLocation{__FILE__, __LINE__});
}

}  // 匿名命名空间

void RunNativeNoiseModeTests(TestRunner &runner, NativeLibraryProbe &library)
{
    const NativeFunctions &functions = library.Functions();
    runner.Run("噪声保持四端口映射、有限值与 0.2% 复数幅度边界，种子及复位可复现", [&functions]() {
        BoundedRepeatableNoise(functions);
    });

    runner.Run("噪声模式在 Init 冻结，失败 Get 不消费、pending 不被下载改变且仅能成功 Get 一次", [&functions]() {
        FrozenModeAndPendingConsumption(functions);
    });
}

}  // 命名空间 VnaTest
