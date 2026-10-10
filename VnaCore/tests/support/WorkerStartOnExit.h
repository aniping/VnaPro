#ifndef VNAPRO_CORE_TEST_WORKER_START_ON_EXIT_H
#define VNAPRO_CORE_TEST_WORKER_START_ON_EXIT_H

#include <atomic>
#include <future>

namespace vna_core_test {

struct WorkerStartOnExit {
    std::promise<void>& signal;
    std::atomic<bool>& finished;
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
