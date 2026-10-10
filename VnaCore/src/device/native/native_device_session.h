/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_NATIVE_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_NATIVE_DEVICE_SESSION_H

#include "device/native/native_library.h"
#include "vna_core/device_session.h"

namespace VnaCore {
namespace Detail {

class NativeDeviceSession final : public DeviceSession {
public:
    // 保留动态库的绝对路径，直到工作线程执行初始化。
    explicit NativeDeviceSession(std::string path);
    NativeDeviceSession(const NativeDeviceSession &) = delete;
    NativeDeviceSession &operator=(const NativeDeviceSession &) = delete;
    NativeDeviceSession(NativeDeviceSession &&) = delete;
    NativeDeviceSession &operator=(NativeDeviceSession &&) = delete;
    ~NativeDeviceSession() override = default;
    // 加载驱动、初始化硬件，并复制原生设备标识响应。
    std::string Initialize() override;
    // 在变更任何硬件配置前，准备好全部下载缓冲区。
    void ApplyPlan(const SweepPlan &plan) override;
    // 在已初始化的原生会话中启用射频。
    void EnableRf() override;
    // 将物理源端口转换为 ABI 索引，并返回独立持有的完整 IQ 缓冲区。
    std::vector<double> AcquirePort(const SweepPlan &plan, std::uint32_t sourcePort) override;
    // 尝试关闭射频，随后始终释放动态库，再报告关闭错误。
    void Shutdown() override;

private:
    const std::string path_;
    NativeLibrary library_;
    bool initialized_ = false;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
