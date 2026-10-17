/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

#include "test_environment.h"
#include "test_support.h"

namespace {

std::atomic<bool> g_failNextWorkerAllocation{false};
std::atomic<unsigned> g_failures{0U};
std::thread::id g_testThread;

void ArmAllocationFailure()
{
    g_testThread = std::this_thread::get_id();
    g_failNextWorkerAllocation.store(true);
}

void VerifyPreviewAllocationFailure(const std::string &library, const std::string &trace)
{
    HardwareBoardTest::TestEnvironment environment(trace, "", 1U, 3U);
    environment.SetReadDelay(550U);
    const auto plan = HardwareBoardTest::Plan({3U, 1U, 4U, 2U}, 256U);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    HardwareBoardTest::Success(core.Initialize());
    HardwareBoardTest::Success(core.Configure(plan));
    const auto run = HardwareBoardTest::Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("GET_BEGIN 128 64 4096");
    const auto retained = core.ReadView().sweepProgress;
    // 已采128点且第三段Get阻塞后注入；下一次核心分配正是显示快照的计划复制。
    ArmAllocationFailure();
    environment.ReleaseRead();
    const auto result = HardwareBoardTest::Await(trigger);
    g_failNextWorkerAllocation.store(false);
    HardwareBoardTest::Require(g_failures.load() == 1U, "preview allocation failure was not exercised");
    HardwareBoardTest::Require(result.ok, "preview memory failure faulted an otherwise complete acquisition");
    HardwareBoardTest::Require(retained && retained->completedPoints == 128U && !core.ReadView().sweepProgress,
        "preview memory failure corrupted retained progress or failed to clear the live preview");
    HardwareBoardTest::VerifyFrame(core.ReadView().frame, plan, 1U);
    HardwareBoardTest::Success(core.Stop(run.runId));
    HardwareBoardTest::Require(core.Close().ok, "preview failure prevented orderly device shutdown");
    environment.VerifyThreadAndEnvironment();
}

}  // 匿名命名空间

// 仅此独立测试程序覆盖分配器；不向正式库或测试探针 DLL 注入分配行为。
void *operator new(std::size_t size)
{
    if (g_failNextWorkerAllocation.load() && std::this_thread::get_id() != g_testThread &&
        g_failNextWorkerAllocation.exchange(false)) {
        g_failures.fetch_add(1U);
        throw std::bad_alloc();
    }
    void *memory = std::malloc(size == 0U ? 1U : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}

int main(int argc, char **argv)
{
    try {
        HardwareBoardTest::Require(argc == 3, "usage: progress allocation test <independent ABI probe> <trace>");
        VerifyPreviewAllocationFailure(argv[1], argv[2]);
        std::cout << "preview allocation failure preserved complete native acquisition\n";
        return 0;
    } catch (const std::exception &error) {
        g_failNextWorkerAllocation.store(false);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
