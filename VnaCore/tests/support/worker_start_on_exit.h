/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_TEST_WORKER_START_ON_EXIT_H
#define VNAPRO_CORE_TEST_WORKER_START_ON_EXIT_H

#include <atomic>
#include <future>

namespace VnaCoreTest {

struct WorkerStartOnExit final {
    // 后续线程创建抛出异常时，确保已启动的工作线程能够退出。
    WorkerStartOnExit(std::promise<void> &start, std::atomic<bool> &done) noexcept : signal(start), finished(done)
    {
    }

    WorkerStartOnExit(const WorkerStartOnExit &) = delete;
    WorkerStartOnExit &operator=(const WorkerStartOnExit &) = delete;
    WorkerStartOnExit(WorkerStartOnExit &&) = delete;
    WorkerStartOnExit &operator=(WorkerStartOnExit &&) = delete;
    std::promise<void> &signal;
    std::atomic<bool> &finished;
    bool pending = true;

    ~WorkerStartOnExit()
    {
        if (pending) {
            finished.store(true);
            signal.set_value();
        }
    }
};

}  // 命名空间 VnaCoreTest

#endif
