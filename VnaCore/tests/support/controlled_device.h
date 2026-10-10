#ifndef VNAPRO_CORE_TEST_CONTROLLED_DEVICE_H
#define VNAPRO_CORE_TEST_CONTROLLED_DEVICE_H

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "concurrency_assertions.h"
#include "device/device_session.h"
#include "device/plan_preparation_error.h"

namespace vna_core_test {

class ControlledDevice : public vna_core::detail::DeviceSession {
public:
    std::mutex mutex;
    std::condition_variable cv;
    bool blockNext = false;
    bool entered = false;
    bool release = false;
    bool failApply = false;
    bool failPrepare = false;
    bool failShutdown = false;
    std::size_t failAcquireAt = 0U;
    std::size_t shortAcquireAt = 0U;
    std::size_t acquisitions = 0U;
    std::uint64_t acquisitionFrame = 0U;
    std::thread::id deviceThread;
    bool wrongThread = false;
    bool shutdownCalled = false;
    vna_core::SweepPlan applied;

    std::string initialize() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
        return "TestDevice";
    }

    void applyPlan(const vna_core::SweepPlan &plan) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
        if (failPrepare) {
            failPrepare = false;
            throw vna_core::detail::PlanPreparationError();
        }
        applied = plan;  // Simulate a partial mutation before reporting failure.
        if (failApply) {
            failApply = false;
            throw std::runtime_error("controlled partial configuration failure");
        }
    }

    void enableRf() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
        acquisitionFrame = 0U;
    }

    std::vector<double> acquirePort(const vna_core::SweepPlan &plan, std::uint32_t source) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        recordThread();
        require(samePlan(plan, applied), "device received a plan different from its applied configuration");
        if (source == plan.sourcePorts.front()) {
            ++acquisitionFrame;
        }
        ++acquisitions;
        cv.notify_all();
        if (blockNext) {
            blockNext = false;
            entered = true;
            cv.notify_all();
            cv.wait(lock, [this] {
                return release;
            });
        }
        if (acquisitions == failAcquireAt) {
            throw std::runtime_error("controlled second-port acquisition failure");
        }
        std::vector<double> iq;
        iq.reserve(16U * plan.frequencyRaw.size());
        for (std::size_t point = 0U; point < plan.frequencyRaw.size(); ++point) {
            for (std::size_t receiver = 0U; receiver < 4U; ++receiver) {
                for (std::size_t component = 0U; component < 4U; ++component) {
                    iq.push_back(sampleValue(plan, acquisitionFrame, source, point, receiver, component));
                }
            }
        }
        if (acquisitions == shortAcquireAt) {
            iq.pop_back();
        }
        return iq;
    }

    void shutdown() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        recordThread();
        shutdownCalled = true;
        if (failShutdown) {
            throw std::runtime_error("controlled RF-off failure");
        }
    }

    void arm()
    {
        std::lock_guard<std::mutex> lock(mutex);
        blockNext = true;
        entered = release = false;
    }

    void waitEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(cv.wait_for(lock, std::chrono::seconds(3),
                    [this] {
                        return entered;
                    }),
            "gate was never entered");
    }

    void releaseGate()
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        cv.notify_all();
    }

    void recordThread()
    {
        if (deviceThread == std::thread::id()) {
            deviceThread = std::this_thread::get_id();
        }
        wrongThread = wrongThread || deviceThread != std::this_thread::get_id();
    }
};

}  // namespace vna_core_test

#endif
