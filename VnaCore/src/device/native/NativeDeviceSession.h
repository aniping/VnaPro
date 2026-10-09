#ifndef VNAPRO_VNA_CORE_NATIVE_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_NATIVE_DEVICE_SESSION_H

#include "device/DeviceSession.h"
#include "device/native/NativeLibrary.h"

namespace vna_core {
namespace detail {

class NativeDeviceSession : public DeviceSession {
public:
    explicit NativeDeviceSession(std::string path);
    std::string initialize() override;
    void applyPlan(const SweepPlan& plan) override;
    void enableRf() override;
    std::vector<double> acquirePort(const SweepPlan& plan, std::uint32_t sourcePort) override;
    void shutdown() override;

private:
    std::string path_;
    NativeLibrary library_;
    bool initialized_ = false;
};

}  // namespace detail
}  // namespace vna_core

#endif
