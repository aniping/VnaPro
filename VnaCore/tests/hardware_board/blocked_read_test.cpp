/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

#include "test_environment.h"
#include "test_support.h"

namespace HardwareBoardTest {
namespace {

bool AwaitCloseAdmission(VnaCore::AcquisitionCore &core)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    // 最多占用 16 个队列位置，避免将队列已满的拒绝误认为关闭已开始。
    for (std::uint32_t attempt = 0U; attempt < 16U && std::chrono::steady_clock::now() < deadline; ++attempt) {
        const auto ticket = core.Start(VnaCore::RunMode::MANUAL);
        if (ticket.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            const auto result = ticket.get();
            if (!result.ok && result.message.find("closing or closed") != std::string::npos) {
                return true;
            }
        }
        // 门闩未释放时不读取未完成的票据；已接受的 START 将由 FIFO 按 RUNNING 状态拒绝。
        const auto delay = 1U << (std::min)(attempt, 7U);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return false;
}

}  // 匿名命名空间

void VerifyCloseWaits(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace, "", 1U, 2U);
    const auto plan = Plan({4U, 2U});
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto trigger = core.TriggerOnce(started.runId, started.configRevision);
    environment.AwaitEvent("GET_BEGIN 64 64 2048");
    auto closing = std::async(std::launch::async, [&core] {
        return core.Close();
    });
    const bool closeEntered = AwaitCloseAdmission(core);
    const bool closeBlocked = closing.wait_for(std::chrono::milliseconds(40)) == std::future_status::timeout;
    const auto pending = core.ReadView();
    const auto beforeRelease = environment.Events();
    const bool prematureOff = std::count(beforeRelease.begin(), beforeRelease.end(), "RF_OFF") != 1;
    const bool unloaded = std::find(beforeRelease.begin(), beforeRelease.end(), "UNLOAD") != beforeRelease.end();
    // 先释放受控读取，再检查断言，避免测试失败时未来对象析构阻塞在门闩上。
    environment.ReleaseRead();
    Success(trigger);
    Require(closing.wait_for(std::chrono::seconds(3)) == std::future_status::ready && closing.get().ok,
        "hardware close did not finish after native read returned");
    Require(closeEntered && closeBlocked && !pending.frame && !prematureOff && !unloaded,
        "hardware close overtook an in-flight read or published a partial frame");
    VerifyFrame(core.ReadView().frame, plan, 1U);
    const auto completed = environment.Events();
    Require(completed[completed.size() - 2U] == "GET_END 128 2 64" && completed.back() == "RF_OFF" &&
                std::find(completed.begin(), completed.end(), "UNLOAD") == completed.end(),
        "hardware close did not finish the read before RF-off or released the unowned driver resources");
    environment.VerifyThreadAndEnvironment();
}

void VerifyContinuousFrames(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace, "", 1U, 2U);
    const auto plan = Plan({3U, 1U, 4U, 2U});
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto started = Success(core.Start(VnaCore::RunMode::CONTINUOUS));
    environment.AwaitEvent("GET_BEGIN 64 64 4096");
    const auto pending = core.ReadView();
    const auto events = environment.Events();
    const bool nextSweepStarted = std::count(events.begin(), events.end(), "START") != 1;
    environment.ReleaseRead();
    Require(!pending.frame && !nextSweepStarted, "continuous acquisition overtook a pending source segment");
    environment.AwaitEvent("GET_END 128 2 128", 2U);
    Success(core.Stop(started.runId));
    const auto held = core.ReadView().frame;
    VerifyFrame(held, plan, static_cast<std::uint32_t>(held->frameSeq));
    Require(core.Close().ok, "continuous hardware close failed");
    const auto completed = environment.Events();
    std::size_t points = TEST_POINT_COUNT;
    for (const auto &event : completed) {
        if (event == "START") {
            Require(points == TEST_POINT_COUNT, "next sweep started before prior final segment returned");
            points = 0U;
        } else if (event.find("GET_END 128 2 ") == 0U) {
            points += 2U;
        } else if (event.find("GET_END ") == 0U) {
            points += 64U;
        }
    }
    Require(points == TEST_POINT_COUNT && completed.back() == "RF_OFF" &&
                std::find(completed.begin(), completed.end(), "UNLOAD") == completed.end(),
        "continuous final frame is incomplete or the driver was unloaded without a release API");
    environment.VerifyThreadAndEnvironment();
}

}  // 命名空间 HardwareBoardTest
