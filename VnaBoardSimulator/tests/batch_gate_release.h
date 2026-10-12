/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_BATCH_GATE_RELEASE_H
#define VNAPRO_BOARD_BATCH_GATE_RELEASE_H

#include "batch_gate.h"

namespace BoardTest {

class BatchGateRelease final {
public:
    // 必须比模拟器晚构造，确保异常退出时先放行工作线程、再析构模拟器。
    explicit BatchGateRelease(BatchGate &gate);
    // 提前释放门闩，使线程回收和异步 future 析构不会互相等待。
    ~BatchGateRelease();
    BatchGateRelease(const BatchGateRelease &) = delete;
    BatchGateRelease &operator=(const BatchGateRelease &) = delete;
    BatchGateRelease(BatchGateRelease &&) = delete;
    BatchGateRelease &operator=(BatchGateRelease &&) = delete;

private:
    BatchGate &gate_;
};

}  // 命名空间 BoardTest
#endif  // VNAPRO_BOARD_BATCH_GATE_RELEASE_H
