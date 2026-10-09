#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "GoldenIqVectors.h"
#include "NativeContractTestSupport.h"

namespace vna_test {
namespace {

void ConfigureNormalSweep(const NativeFunctions& functions)
{
    VNA_EXPECT_EQ(kOk, functions.hwInit());
    VNA_EXPECT_EQ(kOk, functions.setRfPowerOn(1U));
    VNA_EXPECT_EQ(kOk, functions.activateRxLink(0xFU));
    VNA_EXPECT_EQ(kOk, functions.setFastSweepMode(0U));
}

void RunFourPortSweep(const NativeFunctions& functions)
{
    const std::uint32_t pointCount = 3U;
    std::vector<FrameInput> frames;
    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        frames.push_back(MakeNormalFrame(pointCount, sourcePort,
            UINT64_C(1000000000) + static_cast<std::uint64_t>(sourcePort) * UINT64_C(1000000000)));
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frames.back()));
    }

    for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
        const std::vector<double> expected = ExpectedIq(frames[sourcePort].frequencies, sourcePort);
        VNA_EXPECT_EQ(kOk, functions.startSweep(pointCount, sourcePort));

        const double canary = -987654321.125;
        std::vector<double> storage(expected.size() + 2U, canary);
        VNA_EXPECT_EQ(kOk, functions.getSweepData(pointCount, storage.data() + 1U));
        VNA_EXPECT_EQ(canary, storage.front());
        VNA_EXPECT_EQ(canary, storage.back());
        ExpectIqNear(expected, storage.data() + 1U);

        VNA_EXPECT_EQ(kOk, functions.startSweep(pointCount, sourcePort));
        std::vector<double> repeated(expected.size(), 0.0);
        VNA_EXPECT_EQ(kOk, functions.getSweepData(pointCount, repeated.data()));
        ExpectIqNear(expected, repeated.data());
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            VNA_EXPECT_EQ(storage[index + 1U], repeated[index]);
        }
    }
}

bool IqMatches(const std::vector<double>& expected, const double* actual)
{
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (!std::isfinite(actual[index]) || std::fabs(expected[index] - actual[index]) > 1.0e-12) {
            return false;
        }
    }
    return true;
}

}  // namespace

void RunNativeNormalSweepTests(TestRunner* runner, NativeLibraryProbe* library)
{
    const NativeFunctions& functions = library->Functions();

    runner->Run("四个激励端口完成确定性正常扫频并守住 canary", [&functions]() {
        ConfigureNormalSweep(functions);
        RunFourPortSweep(functions);
    });

    runner->Run("动态加载的四端口扫频匹配固定 IQ 黄金向量", [&functions]() {
        ConfigureNormalSweep(functions);
        for (std::uint32_t sourcePort = 0U; sourcePort < 4U; ++sourcePort) {
            FrameInput frame = MakeNormalFrame(2U, sourcePort, kGoldenIqFrequencies[0]);
            frame.frequencies[1] = kGoldenIqFrequencies[1];
            frame.parameters[7U] = static_cast<std::uint32_t>(kGoldenIqFrequencies[1] >> 32U);
            frame.parameters[8U] = static_cast<std::uint32_t>(kGoldenIqFrequencies[1]);
            VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));

            VNA_EXPECT_EQ(kOk, functions.startSweep(2U, sourcePort));
            const std::vector<double> expected = GoldenIqForSource(sourcePort);
            std::vector<double> actual(expected.size(), 0.0);
            VNA_EXPECT_EQ(kOk, functions.getSweepData(2U, actual.data()));
            ExpectIqNear(expected, actual.data());
        }
    });

    runner->Run("动态加载扫频匹配频率高 32 位黄金向量", [&functions]() {
        ConfigureNormalSweep(functions);
        FrameInput frame = MakeNormalFrame(2U, 0U, kGoldenIqFrequencies[0]);
        frame.frequencies[1U] = kGoldenIqHighWordFrequency;
        frame.parameters[7U] = static_cast<std::uint32_t>(kGoldenIqHighWordFrequency >> 32U);
        frame.parameters[8U] = static_cast<std::uint32_t>(kGoldenIqHighWordFrequency);
        VNA_EXPECT_EQ(1U, frame.parameters[7U]);
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));
        VNA_EXPECT_EQ(kOk, functions.startSweep(2U, 0U));

        std::vector<double> actual(32U, 0.0);
        VNA_EXPECT_EQ(kOk, functions.getSweepData(2U, actual.data()));
        for (std::size_t index = 0U; index < 16U; ++index) {
            VNA_EXPECT_NEAR(kGoldenIqBySource[0U][0U][index], actual[index], 1.0e-12);
            VNA_EXPECT_NEAR(kGoldenIqHighWordSource0[index], actual[16U + index], 1.0e-12);
        }
        VNA_EXPECT_NE(actual[1U], actual[17U]);
    });

    runner->Run("下载成功后不引用调用方参数数组", [&functions]() {
        ConfigureNormalSweep(functions);
        FrameInput frame = MakeNormalFrame(2U, 2U, UINT64_C(0x123456789abcdef0));
        const std::vector<std::uint64_t> originalFrequencies = frame.frequencies;
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));

        std::fill(frame.parameters.begin(), frame.parameters.end(), 0xFFFFFFFFU);
        std::fill(frame.powerTable.begin(), frame.powerTable.end(), std::numeric_limits<double>::quiet_NaN());
        frame.parameters.clear();
        frame.powerTable.clear();

        VNA_EXPECT_EQ(kOk, functions.startSweep(2U, 2U));
        std::vector<double> actual(32U, 0.0);
        VNA_EXPECT_EQ(kOk, functions.getSweepData(2U, actual.data()));
        const std::vector<double> expected = ExpectedIq(originalFrequencies, 2U);
        ExpectIqNear(expected, actual.data());
    });

    runner->Run("Start 和 Get 严格执行状态及消费语义", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        VNA_EXPECT_EQ(kRfDisabled, functions.startSweep(2U, 0U));
        VNA_EXPECT_EQ(kOk, functions.setRfPowerOn(1U));
        VNA_EXPECT_EQ(kInvalidState, functions.startSweep(2U, 0U));
        VNA_EXPECT_EQ(kOk, functions.activateRxLink(1U));
        VNA_EXPECT_EQ(kInvalidState, functions.startSweep(2U, 0U));
        VNA_EXPECT_EQ(kOk, functions.setFastSweepMode(0U));
        VNA_EXPECT_EQ(kInvalidState, functions.startSweep(2U, 0U));

        FrameInput frame = MakeNormalFrame(2U, 0U, UINT64_C(1000000000));
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));
        VNA_EXPECT_EQ(kInvalidState, functions.startSweep(1U, 0U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.startSweep(2U, 4U));
        VNA_EXPECT_EQ(kOk, functions.startSweep(2U, 0U));
        VNA_EXPECT_EQ(kInvalidState, functions.startSweep(2U, 0U));

        VNA_EXPECT_EQ(kInvalidArgument, functions.getSweepData(2U, NULL));

        std::vector<double> guarded(34U, 71.25);
        VNA_EXPECT_EQ(kInvalidState, functions.getSweepData(1U, guarded.data() + 1U));
        for (std::size_t index = 0U; index < guarded.size(); ++index) {
            VNA_EXPECT_EQ(71.25, guarded[index]);
        }

        VNA_EXPECT_EQ(kOk, functions.getSweepData(2U, guarded.data() + 1U));
        VNA_EXPECT_EQ(71.25, guarded.front());
        VNA_EXPECT_EQ(71.25, guarded.back());
        VNA_EXPECT_EQ(kInvalidState, functions.getSweepData(2U, guarded.data() + 1U));

        VNA_EXPECT_EQ(kOk, functions.startSweep(2U, 0U));
        VNA_EXPECT_EQ(kOk, functions.setRfPowerOn(0U));
        VNA_EXPECT_EQ(kInvalidState, functions.getSweepData(2U, guarded.data() + 1U));
        VNA_EXPECT_EQ(kRfDisabled, functions.startSweep(2U, 0U));
    });

    runner->Run("C ABI 并发复位与 Start/Get 后仍可恢复", [&functions]() {
        ConfigureNormalSweep(functions);
        FrameInput frame = MakeNormalFrame(2U, 0U, UINT64_C(1000000000));
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));
        const std::vector<double> expected = ExpectedIq(frame.frequencies, 0U);

        std::atomic<std::uint32_t> failures(0U);
        std::atomic<std::uint32_t> successfulStarts(0U);
        std::atomic<std::uint32_t> successfulGets(0U);
        std::atomic<std::uint32_t> readyThreads(0U);
        std::atomic<bool> startThreads(false);
        std::atomic<bool> reconfigureDone(false);
        std::thread reconfigure([&functions, &frame, &failures, &readyThreads, &startThreads, &reconfigureDone]() {
            ++readyThreads;
            while (!startThreads.load()) {
                std::this_thread::yield();
            }
            for (std::uint32_t iteration = 0U; iteration < 200U; ++iteration) {
                if (functions.hwInit() != kOk || functions.setRfPowerOn(1U) != kOk ||
                    functions.activateRxLink(0xFU) != kOk || functions.setFastSweepMode(0U) != kOk ||
                    DownloadFrame(functions, &frame) != kOk) {
                    ++failures;
                }
            }
            reconfigureDone.store(true);
        });
        std::thread sweep([&functions, &expected, &failures, &successfulStarts, &successfulGets, &readyThreads,
                              &startThreads, &reconfigureDone]() {
            ++readyThreads;
            while (!startThreads.load()) {
                std::this_thread::yield();
            }
            const double canary = -314159.25;
            for (std::uint32_t iteration = 0U; iteration < 800U; ++iteration) {
                const std::uint32_t startStatus = functions.startSweep(2U, 0U);
                if (startStatus == kOk) {
                    ++successfulStarts;
                    std::vector<double> guarded(34U, canary);
                    const std::uint32_t getStatus = functions.getSweepData(2U, guarded.data() + 1U);
                    if (getStatus == kOk) {
                        ++successfulGets;
                        if (guarded.front() != canary || guarded.back() != canary ||
                            !IqMatches(expected, guarded.data() + 1U)) {
                            ++failures;
                        }
                    } else if (getStatus == kInvalidState) {
                        for (std::size_t index = 0U; index < guarded.size(); ++index) {
                            if (guarded[index] != canary) {
                                ++failures;
                                break;
                            }
                        }
                    } else {
                        ++failures;
                    }
                } else if (startStatus != kRfDisabled && startStatus != kInvalidState) {
                    ++failures;
                }
            }

            // 等复位线程停下后强制完成一次成功路径，避免压力循环因调度
            // 恰好只命中允许的状态错误而空转通过。
            while (!reconfigureDone.load()) {
                std::this_thread::yield();
            }
            if (functions.startSweep(2U, 0U) != kOk) {
                ++failures;
                return;
            }
            ++successfulStarts;
            std::vector<double> guarded(34U, canary);
            if (functions.getSweepData(2U, guarded.data() + 1U) != kOk) {
                ++failures;
                return;
            }
            ++successfulGets;
            if (guarded.front() != canary || guarded.back() != canary || !IqMatches(expected, guarded.data() + 1U)) {
                ++failures;
            }
        });
        while (readyThreads.load() != 2U) {
            std::this_thread::yield();
        }
        startThreads.store(true);
        reconfigure.join();
        sweep.join();
        VNA_EXPECT_EQ(0U, failures.load());
        VNA_EXPECT_TRUE(successfulStarts.load() > 0U);
        VNA_EXPECT_TRUE(successfulGets.load() > 0U);

        ConfigureNormalSweep(functions);
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));
        VNA_EXPECT_EQ(kOk, functions.startSweep(2U, 0U));
        std::vector<double> recovered(32U, 0.0);
        VNA_EXPECT_EQ(kOk, functions.getSweepData(2U, recovered.data()));
        ExpectIqNear(ExpectedIq(frame.frequencies, 0U), recovered.data());
    });

    runner->Run("模式、RF 和接收位图参数严格校验", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        VNA_EXPECT_EQ(kInvalidArgument, functions.setRfPowerOn(2U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.activateRxLink(0U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.activateRxLink(0x10U));
        VNA_EXPECT_EQ(kOk, functions.activateRxLink(0x5U));
        VNA_EXPECT_EQ(kOk, functions.setFastSweepMode(0U));
        VNA_EXPECT_EQ(kNotSupported, functions.setFastSweepMode(1U));
    });

    runner->Run("正常帧拒绝非法长度、字段和混合端口", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        FrameInput frame = MakeNormalFrame(2U, 1U, UINT64_C(2000000000));

        VNA_EXPECT_EQ(kInvalidArgument,
            functions.dataFrameDownload(2U, frame.parameters.data(), 12U, frame.powerTable.data(), 18U));
        VNA_EXPECT_EQ(kInvalidArgument,
            functions.dataFrameDownload(2U, frame.parameters.data(), 13U, frame.powerTable.data(), 17U));

        frame.parameters[0] = 1U;
        VNA_EXPECT_EQ(kNotSupported, DownloadFrame(functions, &frame));
        frame.parameters[0] = 0U;
        frame.parameters[4U] = 1U;
        VNA_EXPECT_EQ(kNotSupported, DownloadFrame(functions, &frame));
        frame.parameters[4U] = 0U;
        frame.parameters[6U] = 1U;
        VNA_EXPECT_EQ(kNotSupported, DownloadFrame(functions, &frame));
        frame.parameters[6U] = 0U;
        frame.parameters[3U] = 4U;
        VNA_EXPECT_EQ(kInvalidArgument, DownloadFrame(functions, &frame));
        frame.parameters[3U] = 1U;
        frame.parameters[9U] = 2U;
        VNA_EXPECT_EQ(kInvalidArgument, DownloadFrame(functions, &frame));
    });

    runner->Run("功率表保持不透明并接受非有限值", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        FrameInput frame = MakeNormalFrame(1U, 0U, UINT64_C(1000000000));
        frame.powerTable[0] = std::numeric_limits<double>::quiet_NaN();
        frame.powerTable[1] = std::numeric_limits<double>::infinity();
        frame.powerTable[2] = -std::numeric_limits<double>::infinity();
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));
    });

    runner->Run("巨大点数在读取输入前报告尺寸溢出", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        std::uint32_t parameter = 0U;
        double table = 0.0;
        VNA_EXPECT_EQ(kSizeOverflow,
            functions.dataFrameDownload(std::numeric_limits<std::uint32_t>::max(), &parameter,
                std::numeric_limits<std::uint32_t>::max(), &table, std::numeric_limits<std::uint32_t>::max()));
    });

    runner->Run("重新初始化清空下载帧和 pending", [&functions]() {
        ConfigureNormalSweep(functions);
        FrameInput frame = MakeNormalFrame(1U, 0U, UINT64_C(1000000000));
        VNA_EXPECT_EQ(kOk, DownloadFrame(functions, &frame));
        VNA_EXPECT_EQ(kOk, functions.startSweep(1U, 0U));
        VNA_EXPECT_EQ(kOk, functions.hwInit());

        double output[16] = {0.0};
        VNA_EXPECT_EQ(kInvalidState, functions.getSweepData(1U, output));
        VNA_EXPECT_EQ(kRfDisabled, functions.startSweep(1U, 0U));
        VNA_EXPECT_EQ(kOk, functions.setRfPowerOn(1U));
        VNA_EXPECT_EQ(kOk, functions.activateRxLink(1U));
        VNA_EXPECT_EQ(kOk, functions.setFastSweepMode(0U));
        VNA_EXPECT_EQ(kInvalidState, functions.startSweep(1U, 0U));
    });
}

}  // namespace vna_test
