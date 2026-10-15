/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_SIMULATOR_STATE_H
#define VNAPRO_BOARD_SIMULATOR_STATE_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "board_sweep_types.h"

namespace VnaBoard {

class BoardSimulatorState final {
public:
    // 内部批次观察接口用于可控调度测试，不属于 DLL ABI。
    using BatchObserver = std::function<void(std::size_t)>;
    using WaitObserver = std::function<void()>;

    explicit BoardSimulatorState(BatchObserver beforePublish = {}, WaitObserver beforeWait = {});
    ~BoardSimulatorState();
    BoardSimulatorState(const BoardSimulatorState &) = delete;
    BoardSimulatorState &operator=(const BoardSimulatorState &) = delete;
    BoardSimulatorState(BoardSimulatorState &&) = delete;
    BoardSimulatorState &operator=(BoardSimulatorState &&) = delete;

    // 初始化同时清除旧计划和结果，关闭射频。
    std::uint32_t Initialize();
    // 各列表分别复制到暂存计划；合法列表可以修正该列表的暂存错误。
    void SetFrequencies(const std::uint64_t *values, std::uint32_t length) noexcept;
    void SetPowers(const double *values, std::uint32_t length) noexcept;
    void SetBandwidths(const std::uint64_t *values, std::uint32_t length) noexcept;
    void SetWorkPorts(const std::uint32_t *values, std::uint32_t length) noexcept;
    // 校验全部暂存项并原子替换已提交计划；失败保留旧计划。
    std::uint32_t Download();
    // 关闭射频会取消扫描并回收工作线程，可作为 DLL 卸载前的退出步骤。
    std::uint32_t SetRfPower(std::uint32_t enabled);
    // 冻结已提交计划并异步启动一次扫描，拒绝并发启动。
    std::uint32_t Start();
    // 阻塞到所需频点全部完成，失败保留输出。
    std::uint32_t Get(std::uint32_t start, std::uint32_t count, double *output, int capacity);

private:
    enum class ListKind {
        FREQUENCIES,
        BANDWIDTHS
    };

    void SetList(ListKind kind, const std::uint64_t *values, std::uint32_t length) noexcept;
    void StopWorker();
    std::uint32_t LaunchSweep();
    void Produce(std::shared_ptr<SweepResult> result, SignalSettings settings, std::uint64_t sweepNumber) noexcept;

    // 生命周期锁串行化初始化、启停与线程回收；数据锁保护暂存、结果和条件变量。
    std::mutex lifecycleMutex_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    BatchObserver beforePublish_;
    WaitObserver beforeWait_;
    StagedPlan staged_;
    std::shared_ptr<const SweepPlan> committed_;
    std::shared_ptr<SweepResult> result_;
    SignalSettings settings_;
    std::uint64_t sweepNumber_ = 0U;
    bool initialized_ = false;
    bool rfEnabled_ = false;
    bool scanning_ = false;
};

}  // 命名空间 VnaBoard
#endif  // VNAPRO_BOARD_SIMULATOR_STATE_H
