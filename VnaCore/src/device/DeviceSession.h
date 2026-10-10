#ifndef VNAPRO_VNA_CORE_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_DEVICE_SESSION_H

#include <cstdint>
#include <string>
#include <vector>

#include "vna_core/SweepPlan.h"

namespace vna_core {
namespace detail {

// Called only by the core worker. Failures throw with the failing device step.
class DeviceSession {
public:
    virtual ~DeviceSession() = default;
    virtual std::string initialize() = 0;
    virtual void applyPlan(const SweepPlan &plan) = 0;
    virtual void enableRf() = 0;
    virtual std::vector<double> acquirePort(const SweepPlan &plan, std::uint32_t sourcePort) = 0;
    virtual void shutdown() = 0;
};

}  // namespace detail
}  // namespace vna_core

#endif
