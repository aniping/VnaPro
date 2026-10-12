/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_BATCH_GATE_H
#define VNAPRO_BOARD_BATCH_GATE_H

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace BoardTest {

class BatchGate final {
public:
    // 指定必须暂停发布的批次结束点。
    explicit BatchGate(std::size_t heldEnd);
    // 保留独立兜底释放，测试还需额外保证析构顺序。
    ~BatchGate();
    BatchGate(const BatchGate &) = delete;
    BatchGate &operator=(const BatchGate &) = delete;
    BatchGate(BatchGate &&) = delete;
    BatchGate &operator=(BatchGate &&) = delete;
    // 工作线程发布前到达此处，命中指定批次时等待放行。
    void Observe(std::size_t end);
    // 控制线程等待确认已暂停，不以睡眠猜测扫描进度。
    void WaitUntilHeld();
    // 唤醒受控批次，重复调用不会产生额外动作。
    void Release();

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t heldEnd_;
    bool held_ = false;
    bool released_ = false;
};

}  // 命名空间 BoardTest
#endif  // VNAPRO_BOARD_BATCH_GATE_H
