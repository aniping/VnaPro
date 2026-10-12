/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "batch_gate.h"

#include <chrono>

#include "test_support.h"

namespace BoardTest {

BatchGate::BatchGate(std::size_t heldEnd) : heldEnd_(heldEnd)
{
}

BatchGate::~BatchGate()
{
    Release();
}

void BatchGate::Observe(std::size_t end)
{
    if (end != heldEnd_) {
        return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    held_ = true;
    changed_.notify_all();
    changed_.wait(lock, [this] {
        return released_;
    });
}

void BatchGate::WaitUntilHeld()
{
    std::unique_lock<std::mutex> lock(mutex_);
    Require(changed_.wait_for(lock, std::chrono::seconds(2),
                [this] {
                    return held_;
                }),
        "工作线程未到达受控批次");
}

void BatchGate::Release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    changed_.notify_all();
}

}  // 命名空间 BoardTest
