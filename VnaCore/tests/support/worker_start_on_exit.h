/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_TEST_WORKER_START_ON_EXIT_H
#define VNAPRO_CORE_TEST_WORKER_START_ON_EXIT_H

#include <atomic>
#include <future>

namespace vna_core_test {

struct WorkerStartOnExit final {
    // Ensures already launched workers can exit if later thread creation throws.
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

}  // namespace vna_core_test

#endif
