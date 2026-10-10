#ifndef VNAPRO_SERVICE_CONTROLLED_DEVICE_H
#define VNAPRO_SERVICE_CONTROLLED_DEVICE_H

#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "device/DeviceSession.h"
#include "vna_core/AcquisitionCore.h"
#include "vna_service/InstrumentService.h"

namespace vna_core {
namespace detail {
class CoreTestAccess {
public:
    static std::unique_ptr<AcquisitionCore> create(std::unique_ptr<DeviceSession> device)
    {
        return std::unique_ptr<AcquisitionCore>(new AcquisitionCore(std::move(device)));
    }
};
}  // namespace detail
}  // namespace vna_core
namespace vna_service_test {
class ControlledDevice : public vna_core::detail::DeviceSession {
public:
    std::mutex mutex;
    std::condition_variable cv;
    bool blocked = false;
    bool entered = false;
    bool released = false;
    bool failAcquire = false;
    bool invalidSamples = false;
    bool blockPlan = false;
    bool planEntered = false;
    bool planReleased = false;
    std::string initialize() override { return "VnaPro,NativeSimulator,TEST,1.0"; }
    void applyPlan(const vna_core::SweepPlan&) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (blockPlan) {
            planEntered = true;
            cv.notify_all();
            cv.wait(lock, [this] { return planReleased; });
        }
    }
    void enableRf() override {}
    void shutdown() override {}
    std::vector<double> acquirePort(const vna_core::SweepPlan& plan, std::uint32_t) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (blocked) {
            entered = true;
            cv.notify_all();
            cv.wait(lock, [this] { return released; });
        }
        if (failAcquire)
            throw std::runtime_error("controlled acquisition failure");
        std::vector<double> iq(plan.frequencyRaw.size() * 16U, 1.11);
        if (invalidSamples && plan.frequencyRaw.size() >= 3U) {
            iq[16U] = std::numeric_limits<double>::quiet_NaN();
            iq[33U] = std::numeric_limits<double>::infinity();
        }
        return iq;
    }
    void setInvalidSamples(bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        invalidSamples = value;
    }
    bool hasEntered()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return entered;
    }
    bool hasPlanEntered()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return planEntered;
    }
    void armPlan()
    {
        std::lock_guard<std::mutex> lock(mutex);
        blockPlan = true;
        planEntered = planReleased = false;
    }
    void arm(bool fail = false)
    {
        std::lock_guard<std::mutex> lock(mutex);
        blocked = true;
        entered = released = false;
        failAcquire = fail;
    }
    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        blocked = false;
        planReleased = true;
        blockPlan = false;
        cv.notify_all();
    }
};
struct Fixture {
    ControlledDevice* device;
    std::unique_ptr<vna_core::AcquisitionCore> core;
    std::unique_ptr<vna_service::InstrumentService> service;
    Fixture()
    {
        std::unique_ptr<ControlledDevice> owned(new ControlledDevice);
        device = owned.get();
        core = vna_core::detail::CoreTestAccess::create(std::move(owned));
        if (!core->initialize().get().ok)
            throw std::runtime_error("controlled initialization");
        service.reset(new vna_service::InstrumentService(*core));
        std::string error;
        if (!service->bootstrapMockProfile(error))
            throw std::runtime_error(error);
    }
    ~Fixture()
    {
        device->release();
        core->close();
    }
};
}  // namespace vna_service_test
#endif
