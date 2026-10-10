/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_NATIVE_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_NATIVE_DEVICE_SESSION_H

#include "device/native/native_library.h"
#include "vna_core/device_session.h"

namespace vna_core {
namespace detail {

class NativeDeviceSession final : public DeviceSession {
public:
    // The absolute library path is retained until worker initialization.
    explicit NativeDeviceSession(std::string path);
    NativeDeviceSession(const NativeDeviceSession &) = delete;
    NativeDeviceSession &operator=(const NativeDeviceSession &) = delete;
    NativeDeviceSession(NativeDeviceSession &&) = delete;
    NativeDeviceSession &operator=(NativeDeviceSession &&) = delete;
    ~NativeDeviceSession() override = default;
    // Loads the driver, initializes hardware, and copies the native identification response.
    std::string Initialize() override;
    // Prepares every download buffer before mutating any hardware configuration.
    void ApplyPlan(const SweepPlan &plan) override;
    // Enables RF on the initialized native session.
    void EnableRf() override;
    // Converts a physical source port to the ABI index and returns a complete owned IQ buffer.
    std::vector<double> AcquirePort(const SweepPlan &plan, std::uint32_t sourcePort) override;
    // Attempts RF off, then always releases the library before reporting a shutdown error.
    void Shutdown() override;

private:
    const std::string path_;
    NativeLibrary library_;
    bool initialized_ = false;
};

}  // namespace detail
}  // namespace vna_core

#endif
