/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_DEVICE_SESSION_H

#include <cstdint>
#include <string>
#include <vector>

#include "vna_core/sweep_plan.h"

namespace vna_core {
namespace Detail {

// Called only by the core worker. Failures throw with the failing device step.
class DeviceSession {
public:
    DeviceSession() = default;
    DeviceSession(const DeviceSession &) = delete;
    DeviceSession &operator=(const DeviceSession &) = delete;
    DeviceSession(DeviceSession &&) = delete;
    DeviceSession &operator=(DeviceSession &&) = delete;
    // Allows destruction through the worker-owned polymorphic device interface.
    virtual ~DeviceSession() = default;
    // Initializes the device and returns its identity; failures report the failing native step.
    virtual std::string Initialize() = 0;
    // Applies one owned plan on the worker; preparation failures must precede device mutation.
    virtual void ApplyPlan(const SweepPlan &plan) = 0;
    // Enables RF before a run begins; invoked only by the core worker.
    virtual void EnableRf() = 0;
    // Returns one complete point-major 16-value IQ record per frequency for physical ports 1..4.
    virtual std::vector<double> AcquirePort(const SweepPlan &plan, std::uint32_t sourcePort) = 0;
    // Attempts RF off and releases device resources, reporting any native failure.
    virtual void Shutdown() = 0;
};

}  // namespace Detail
}  // namespace vna_core

#endif
