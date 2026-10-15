/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>

#include "batch_gate.h"
#include "batch_gate_release.h"
#include "test_support.h"
#include "vna_board/board_api_contract.h"

namespace BoardTest {
namespace {

struct CancellationCheck {
    bool firstAwoke;
    bool wholeAwoke;
    std::uint32_t firstStatus;
    std::uint32_t wholeStatus;
    std::uint32_t stopStatus;
};

std::future<std::uint32_t> ReadAsync(
    VnaBoard::BoardSimulatorState &state, std::uint32_t count, std::vector<double> &values)
{
    return std::async(std::launch::async, [&state, count, &values] {
        return state.Get(0U, count, values.data(), static_cast<int>(values.size()));
    });
}

std::future<std::uint32_t> StopAsync(VnaBoard::BoardSimulatorState &state, bool reinitialize)
{
    return std::async(std::launch::async, [&state, reinitialize] {
        return reinitialize ? state.Initialize() : state.SetRfPower(0U);
    });
}

void VerifyCancellation(
    const CancellationCheck &check, const std::vector<double> &first, const std::vector<double> &whole)
{
    Require(check.firstAwoke && check.wholeAwoke, "取消必须唤醒全部等待者");
    Require(check.firstStatus == VnaBoard::DRV_SIM_CANCELLED && check.wholeStatus == VnaBoard::DRV_SIM_CANCELLED,
        "取消必须使待完成读取失败");
    Require(check.stopStatus == VnaBoard::DRV_SIM_OK, "取消及线程回收失败");
    Require(std::all_of(first.begin(), first.end(),
                [](double value) {
                    return value == -123.0;
                }) &&
                std::all_of(whole.begin(), whole.end(),
                    [](double value) {
                        return value == -123.0;
                    }),
        "取消修改了输出");
}

void TestPartialAndBusy()
{
    BatchGate gate(4U);
    std::vector<double> prefix(64U, -1.0);
    VnaBoard::BoardSimulatorState state([&gate](std::size_t end) {
        gate.Observe(end);
    });
    std::future<std::vector<double>> whole;
    BatchGateRelease release(gate);
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "初始化失败");
    Prepare(state, 64U, {1U, 4U});
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "异步启动失败");
    gate.WaitUntilHeld();
    const auto busyStart = state.Start();
    const auto busyDownload = state.Download();
    const auto partialStatus = state.Get(0U, 2U, prefix.data(), 64);
    whole = std::async(std::launch::async, [&state] {
        return Read(state, 64U, 2U);
    });
    const bool blocked = whole.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    gate.Release();
    const auto values = whole.get();
    Require(busyStart == VnaBoard::DRV_SIM_BUSY, "未完成扫描期间必须拒绝再次启动");
    Require(busyDownload == VnaBoard::DRV_SIM_BUSY, "扫描期间不能替换活动计划");
    Require(partialStatus == VnaBoard::DRV_SIM_OK, "已完成前缀应可在后续频点未完成时读取");
    Require(blocked, "完整范围应等待末频点");
    Require(std::equal(prefix.begin(), prefix.begin() + 32, values.begin()) &&
                std::equal(prefix.begin() + 32, prefix.end(), values.begin() + 1024),
        "分段结果与完整结果不符");
}

void TestCancellation(bool reinitialize)
{
    BatchGate gate(1U);
    std::vector<double> first(16U, -123.0);
    std::vector<double> whole(32U, -123.0);
    std::promise<void> waiting;
    std::atomic<std::uint32_t> readerCount{0U};
    VnaBoard::BoardSimulatorState state(
        [&gate](std::size_t end) {
            gate.Observe(end);
        },
        [&] {
            if (readerCount.fetch_add(1U) == 1U) {
                waiting.set_value();
            }
        });
    std::future<std::uint32_t> firstReader;
    std::future<std::uint32_t> wholeReader;
    std::future<std::uint32_t> stop;
    BatchGateRelease release(gate);
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "初始化失败");
    Prepare(state, 2U);
    Require(state.Start() == VnaBoard::DRV_SIM_OK, "异步启动失败");
    gate.WaitUntilHeld();
    firstReader = ReadAsync(state, 1U, first);
    wholeReader = ReadAsync(state, 2U, whole);
    waiting.get_future().wait();
    stop = StopAsync(state, reinitialize);
    const bool firstAwoke = firstReader.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const bool wholeAwoke = wholeReader.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    gate.Release();
    const auto firstStatus = firstReader.get();
    const auto wholeStatus = wholeReader.get();
    const auto stopStatus = stop.get();
    VerifyCancellation({firstAwoke, wholeAwoke, firstStatus, wholeStatus, stopStatus}, first, whole);
}

void TestFrameCompletionBoundary()
{
    VnaBoard::BoardSimulatorState state;
    Require(state.Initialize() == VnaBoard::DRV_SIM_OK, "初始化失败");
    Prepare(state, 1U, {1U, 2U, 3U, 4U});
    for (std::size_t sweep = 0U; sweep < 100U; ++sweep) {
        Require(state.Start() == VnaBoard::DRV_SIM_OK, "完整结果返回后立即再次启动不能误报 busy");
        const auto values = Read(state, 1U, 4U);
        Require(values.size() == 64U, "四端口完整帧大小错误");
    }
    Require(state.SetRfPower(0U) == VnaBoard::DRV_SIM_OK, "卸载前线程回收失败");
}

}  // 匿名命名空间

void TestAsync()
{
    TestPartialAndBusy();
    TestCancellation(false);
    TestCancellation(true);
    TestFrameCompletionBoundary();
}

}  // 命名空间 BoardTest
