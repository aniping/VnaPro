/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "golden_iq_vectors.h"
#include "joined_threads.h"
#include "native_contract_test_support.h"

namespace VnaTest {
namespace {

struct NativeConcurrencyCounters final {
    // 计数器由工作线程共享；只要线程仍可能访问，就不复制或移动计数器。
    NativeConcurrencyCounters() noexcept = default;
    NativeConcurrencyCounters(const NativeConcurrencyCounters &) = delete;
    NativeConcurrencyCounters &operator=(const NativeConcurrencyCounters &) = delete;
    NativeConcurrencyCounters(NativeConcurrencyCounters &&) = delete;
    NativeConcurrencyCounters &operator=(NativeConcurrencyCounters &&) = delete;

    std::atomic<std::uint32_t> failures{0U};
    std::atomic<std::uint32_t> successfulStarts{0U};
    std::atomic<std::uint32_t> successfulGets{0U};
    std::atomic<std::uint32_t> readyThreads{0U};
    std::atomic<bool> startThreads{false};
    std::atomic<bool> reconfigureDone{false};
};

void ConfigureNormalSweep(const NativeFunctions &functions)
{
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.activateRxLink(0xFU) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setFastSweepMode(0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
}

void RunFourPortSweep(const NativeFunctions &functions)
{
    const std::uint32_t pointCount = 3U;
    std::vector<FrameInput> frames;
    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        frames.push_back(MakeNormalFrame(
            pointCount, sourcePort, 1000000000ULL + static_cast<std::uint64_t>(sourcePort) * 1000000000ULL));
        VnaTest::Expect(DownloadFrame(functions, frames.back()) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    }

    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        const std::vector<double> expected = ExpectedIq(frames[sourcePort].frequencies, sourcePort);
        VnaTest::Expect(functions.startSweep(pointCount, sourcePort) == OK, VnaTest::TestLocation{__FILE__, __LINE__});

        const double canary = -987654321.125;
        std::vector<double> storage(expected.size() + 2U, canary);
        VnaTest::Expect(
            functions.getSweepData(pointCount, storage.data() + 1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(storage.front() == canary, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::Expect(storage.back() == canary, VnaTest::TestLocation{__FILE__, __LINE__});
        ExpectIqNear(expected, storage.data() + 1U);

        VnaTest::Expect(functions.startSweep(pointCount, sourcePort) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
        std::vector<double> repeated(expected.size(), 0.0);
        VnaTest::Expect(
            functions.getSweepData(pointCount, repeated.data()) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
        ExpectIqNear(expected, repeated.data());
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            VnaTest::Expect(repeated[index] == storage[index + 1U], VnaTest::TestLocation{__FILE__, __LINE__});
        }
    }
}

bool IqMatches(const std::vector<double> &expected, const double *actual)
{
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (!std::isfinite(actual[index]) || std::fabs(expected[index] - actual[index]) > 1.0e-12) {
            return false;
        }
    }
    return true;
}

void FourPortCanarySweep(const NativeFunctions &functions)
{
    ConfigureNormalSweep(functions);
    RunFourPortSweep(functions);
}

void GoldenFourPortVector(const NativeFunctions &functions)
{
    ConfigureNormalSweep(functions);
    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        FrameInput frame = MakeNormalFrame(2U, sourcePort, g_goldenIqFrequencies[0]);
        frame.frequencies[1] = g_goldenIqFrequencies[1];
        frame.parameters[7U] = static_cast<std::uint32_t>(g_goldenIqFrequencies[1] >> 32U);
        frame.parameters[8U] = static_cast<std::uint32_t>(g_goldenIqFrequencies[1]);
        VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});

        VnaTest::Expect(functions.startSweep(2U, sourcePort) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
        const std::vector<double> expected = GoldenIqForSource(sourcePort);
        std::vector<double> actual(expected.size(), 0.0);
        VnaTest::Expect(functions.getSweepData(2U, actual.data()) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
        ExpectIqNear(expected, actual.data());
    }
}

void HighWordGoldenVector(const NativeFunctions &functions)
{
    ConfigureNormalSweep(functions);
    FrameInput frame = MakeNormalFrame(2U, 0U, g_goldenIqFrequencies[0]);
    frame.frequencies[1U] = GOLDEN_IQ_HIGH_WORD_FREQUENCY;
    frame.parameters[7U] = static_cast<std::uint32_t>(GOLDEN_IQ_HIGH_WORD_FREQUENCY >> 32U);
    frame.parameters[8U] = static_cast<std::uint32_t>(GOLDEN_IQ_HIGH_WORD_FREQUENCY);
    VnaTest::Expect(frame.parameters[7U] == 1U, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});

    std::vector<double> actual(32U, 0.0);
    VnaTest::Expect(functions.getSweepData(2U, actual.data()) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < 16U; ++index) {
        VnaTest::ExpectNear(
            g_goldenIqBySource[0U][0U][index], actual[index], 1.0e-12, VnaTest::TestLocation{__FILE__, __LINE__});
        VnaTest::ExpectNear(
            g_goldenIqHighWordSource0[index], actual[16U + index], 1.0e-12, VnaTest::TestLocation{__FILE__, __LINE__});
    }
    VnaTest::Expect(actual[17U] != actual[1U], VnaTest::TestLocation{__FILE__, __LINE__});
}

void OwnedInputBuffers(const NativeFunctions &functions)
{
    ConfigureNormalSweep(functions);
    FrameInput frame = MakeNormalFrame(2U, 2U, 0x123456789abcdef0ULL);
    const std::vector<std::uint64_t> originalFrequencies = frame.frequencies;
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});

    std::fill(frame.parameters.begin(), frame.parameters.end(), 0xFFFFFFFFU);
    std::fill(frame.powerTable.begin(), frame.powerTable.end(), std::numeric_limits<double>::quiet_NaN());
    frame.parameters.clear();
    frame.powerTable.clear();

    VnaTest::Expect(functions.startSweep(2U, 2U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    std::vector<double> actual(32U, 0.0);
    VnaTest::Expect(functions.getSweepData(2U, actual.data()) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    const std::vector<double> expected = ExpectedIq(originalFrequencies, 2U);
    ExpectIqNear(expected, actual.data());
}

void PendingConsumptionAndRfCancellation(const NativeFunctions &functions)
{
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == RF_DISABLED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.activateRxLink(1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setFastSweepMode(0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});

    FrameInput frame = MakeNormalFrame(2U, 0U, 1000000000ULL);
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(1U, 0U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 4U) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(functions.getSweepData(2U, nullptr) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});

    std::vector<double> guarded(34U, 71.25);
    VnaTest::Expect(
        functions.getSweepData(1U, guarded.data() + 1U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    for (std::size_t index = 0U; index < guarded.size(); ++index) {
        VnaTest::Expect(guarded[index] == 71.25, VnaTest::TestLocation{__FILE__, __LINE__});
    }

    VnaTest::Expect(functions.getSweepData(2U, guarded.data() + 1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(guarded.front() == 71.25, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(guarded.back() == 71.25, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        functions.getSweepData(2U, guarded.data() + 1U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});

    VnaTest::Expect(functions.startSweep(2U, 0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        functions.getSweepData(2U, guarded.data() + 1U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(2U, 0U) == RF_DISABLED, VnaTest::TestLocation{__FILE__, __LINE__});
}

constexpr std::uint32_t RECONFIGURE_ITERATIONS = 200U;
constexpr std::uint32_t SWEEP_ITERATIONS = 800U;
constexpr std::uint32_t CONCURRENT_POINT_COUNT = 2U;
constexpr std::size_t CONCURRENT_WORKER_COUNT = 2U;
constexpr double CONCURRENT_CANARY = -314159.25;

void WaitForConcurrentStart(NativeConcurrencyCounters &counters) noexcept
{
    ++counters.readyThreads;
    while (!counters.startThreads.load()) {
        std::this_thread::yield();
    }
}

void ConcurrentReconfigure(
    const NativeFunctions &functions, FrameInput &frame, NativeConcurrencyCounters &counters) noexcept
{
    WaitForConcurrentStart(counters);
    for (std::uint32_t iteration = 0U; iteration < RECONFIGURE_ITERATIONS; ++iteration) {
        if (functions.hwInit() != OK || functions.setRfPowerOn(1U) != OK || functions.activateRxLink(0xFU) != OK ||
            functions.setFastSweepMode(0U) != OK || DownloadFrame(functions, frame) != OK) {
            ++counters.failures;
        }
    }
    counters.reconfigureDone.store(true);
}

void ValidateConcurrentRead(const NativeFunctions &functions, const std::vector<double> &expected,
    NativeConcurrencyCounters &counters, bool allowReset)
{
    std::vector<double> guarded(expected.size() + 2U, CONCURRENT_CANARY);
    const std::uint32_t status = functions.getSweepData(CONCURRENT_POINT_COUNT, guarded.data() + 1U);
    if (status == OK) {
        ++counters.successfulGets;
        if (guarded.front() != CONCURRENT_CANARY || guarded.back() != CONCURRENT_CANARY ||
            !IqMatches(expected, guarded.data() + 1U)) {
            ++counters.failures;
        }
    } else if (status == INVALID_STATE && allowReset) {
        if (!std::all_of(guarded.begin(), guarded.end(), [](double value) {
                return value == CONCURRENT_CANARY;
            })) {
            ++counters.failures;
        }
    } else {
        ++counters.failures;
    }
}

void ConcurrentSweepAttempt(const NativeFunctions &functions, const std::vector<double> &expected,
    NativeConcurrencyCounters &counters, bool allowReset)
{
    const std::uint32_t status = functions.startSweep(CONCURRENT_POINT_COUNT, 0U);
    if (status == OK) {
        ++counters.successfulStarts;
        ValidateConcurrentRead(functions, expected, counters, allowReset);
    } else if (!allowReset || (status != RF_DISABLED && status != INVALID_STATE)) {
        ++counters.failures;
    }
}

void ConcurrentAcquire(
    const NativeFunctions &functions, const std::vector<double> &expected, NativeConcurrencyCounters &counters) noexcept
{
    WaitForConcurrentStart(counters);
    try {
        for (std::uint32_t iteration = 0U; iteration < SWEEP_ITERATIONS; ++iteration) {
            ConcurrentSweepAttempt(functions, expected, counters, true);
        }
        // 重置停止后强制执行成功路径；仅出现允许的状态错误不能使测试通过。
        while (!counters.reconfigureDone.load()) {
            std::this_thread::yield();
        }
        ConcurrentSweepAttempt(functions, expected, counters, false);
    } catch (...) {
        // 记录工作线程的分配失败，并由所属线程组正常等待其结束。
        ++counters.failures;
    }
}

void ConcurrentResetAndAcquisition(const NativeFunctions &functions)
{
    ConfigureNormalSweep(functions);
    FrameInput frame = MakeNormalFrame(CONCURRENT_POINT_COUNT, 0U, 1000000000ULL);
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, {__FILE__, __LINE__});
    const std::vector<double> expected = ExpectedIq(frame.frequencies, 0U);
    NativeConcurrencyCounters counters;
    // 在线程组引用的帧、预期值和计数器离开作用域前，等待全部线程结束；部分启动失败时也如此。
    JoinedThreads workers(CONCURRENT_WORKER_COUNT, [&counters]() {
        counters.startThreads.store(true);
    });
    workers.Launch([&functions, &frame, &counters]() {
        ConcurrentReconfigure(functions, frame, counters);
    });
    workers.Launch([&functions, &expected, &counters]() {
        ConcurrentAcquire(functions, expected, counters);
    });
    while (counters.readyThreads.load() != CONCURRENT_WORKER_COUNT) {
        std::this_thread::yield();
    }
    workers.Join();
    VnaTest::Expect(counters.failures.load() == 0U, {__FILE__, __LINE__});
    VnaTest::Expect(counters.successfulStarts.load() > 0U, {__FILE__, __LINE__});
    VnaTest::Expect(counters.successfulGets.load() > 0U, {__FILE__, __LINE__});
    ConfigureNormalSweep(functions);
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, {__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(CONCURRENT_POINT_COUNT, 0U) == OK, {__FILE__, __LINE__});
    std::vector<double> recovered(expected.size(), 0.0);
    VnaTest::Expect(functions.getSweepData(CONCURRENT_POINT_COUNT, recovered.data()) == OK, {__FILE__, __LINE__});
    ExpectIqNear(expected, recovered.data());
}

void ConfigurationArgumentValidation(const NativeFunctions &functions)
{
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(2U) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.activateRxLink(0U) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.activateRxLink(0x10U) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.activateRxLink(0x5U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setFastSweepMode(0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setFastSweepMode(1U) == NOT_SUPPORTED, VnaTest::TestLocation{__FILE__, __LINE__});
}

void FrameFieldValidation(const NativeFunctions &functions)
{
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    FrameInput frame = MakeNormalFrame(2U, 1U, 2000000000ULL);

    VnaTest::Expect(
        functions.dataFrameDownload(2U, frame.parameters.data(), 12U, frame.powerTable.data(), 18U) == INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(
        functions.dataFrameDownload(2U, frame.parameters.data(), 13U, frame.powerTable.data(), 17U) == INVALID_ARGUMENT,
        VnaTest::TestLocation{__FILE__, __LINE__});

    frame.parameters[0] = 1U;
    VnaTest::Expect(DownloadFrame(functions, frame) == NOT_SUPPORTED, VnaTest::TestLocation{__FILE__, __LINE__});
    frame.parameters[0] = 0U;
    frame.parameters[4U] = 1U;
    VnaTest::Expect(DownloadFrame(functions, frame) == NOT_SUPPORTED, VnaTest::TestLocation{__FILE__, __LINE__});
    frame.parameters[4U] = 0U;
    frame.parameters[6U] = 1U;
    VnaTest::Expect(DownloadFrame(functions, frame) == NOT_SUPPORTED, VnaTest::TestLocation{__FILE__, __LINE__});
    frame.parameters[6U] = 0U;
    frame.parameters[3U] = 4U;
    VnaTest::Expect(DownloadFrame(functions, frame) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
    frame.parameters[3U] = 1U;
    frame.parameters[9U] = 2U;
    VnaTest::Expect(DownloadFrame(functions, frame) == INVALID_ARGUMENT, VnaTest::TestLocation{__FILE__, __LINE__});
}

void OpaquePowerTable(const NativeFunctions &functions)
{
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    FrameInput frame = MakeNormalFrame(1U, 0U, 1000000000ULL);
    frame.powerTable[0] = std::numeric_limits<double>::quiet_NaN();
    frame.powerTable[1] = std::numeric_limits<double>::infinity();
    frame.powerTable[2] = -std::numeric_limits<double>::infinity();
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
}

void ShapeOverflowBeforeRead(const NativeFunctions &functions)
{
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    std::uint32_t parameter = 0U;
    double table = 0.0;
    VnaTest::Expect(functions.dataFrameDownload(std::numeric_limits<std::uint32_t>::max(), &parameter,
                        std::numeric_limits<std::uint32_t>::max(), &table,
                        std::numeric_limits<std::uint32_t>::max()) == SIZE_OVERFLOW,
        VnaTest::TestLocation{__FILE__, __LINE__});
}

void ResetClearsFrameAndPending(const NativeFunctions &functions)
{
    ConfigureNormalSweep(functions);
    FrameInput frame = MakeNormalFrame(1U, 0U, 1000000000ULL);
    VnaTest::Expect(DownloadFrame(functions, frame) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(1U, 0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.hwInit() == OK, VnaTest::TestLocation{__FILE__, __LINE__});

    double output[16] = {0.0};
    VnaTest::Expect(functions.getSweepData(1U, output) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(1U, 0U) == RF_DISABLED, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setRfPowerOn(1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.activateRxLink(1U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.setFastSweepMode(0U) == OK, VnaTest::TestLocation{__FILE__, __LINE__});
    VnaTest::Expect(functions.startSweep(1U, 0U) == INVALID_STATE, VnaTest::TestLocation{__FILE__, __LINE__});
}

}  // 匿名命名空间

void RunNativeNormalSweepTests(TestRunner &runner, NativeLibraryProbe &library)
{
    const NativeFunctions &functions = library.Functions();

    runner.Run("四个激励端口完成确定性正常扫频并守住 canary", [&functions]() {
        FourPortCanarySweep(functions);
    });

    runner.Run("动态加载的四端口扫频匹配固定 IQ 黄金向量", [&functions]() {
        GoldenFourPortVector(functions);
    });

    runner.Run("动态加载扫频匹配频率高 32 位黄金向量", [&functions]() {
        HighWordGoldenVector(functions);
    });

    runner.Run("下载成功后不引用调用方参数数组", [&functions]() {
        OwnedInputBuffers(functions);
    });

    runner.Run("Start 和 Get 严格执行状态及消费语义", [&functions]() {
        PendingConsumptionAndRfCancellation(functions);
    });

    runner.Run("C ABI 并发复位与 Start/Get 后仍可恢复", [&functions]() {
        ConcurrentResetAndAcquisition(functions);
    });

    runner.Run("模式、RF 和接收位图参数严格校验", [&functions]() {
        ConfigurationArgumentValidation(functions);
    });

    runner.Run("正常帧拒绝非法长度、字段和混合端口", [&functions]() {
        FrameFieldValidation(functions);
    });

    runner.Run("功率表保持不透明并接受非有限值", [&functions]() {
        OpaquePowerTable(functions);
    });

    runner.Run("巨大点数在读取输入前报告尺寸溢出", [&functions]() {
        ShapeOverflowBeforeRead(functions);
    });

    runner.Run("重新初始化清空下载帧和 pending", [&functions]() {
        ResetClearsFrameAndPending(functions);
    });
}

}  // 命名空间 VnaTest
