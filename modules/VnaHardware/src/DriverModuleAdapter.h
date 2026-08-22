#ifndef VNA_HARDWARE_DRIVER_MODULE_ADAPTER_H
#define VNA_HARDWARE_DRIVER_MODULE_ADAPTER_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Result.h"
#include "VnaDriverSdk/VnaDriverApi.h"
#include "VnaHardware.h"

namespace vna::detail {

// 模块对象保证动态库晚于 API 表和所有设备句柄释放，并串行化模块级回调。
class DriverModule final {
public:
    DriverModule(void *libraryHandle, std::string path, VnaDriverApiV1 api, VnaDriverHandle driverHandle);
    ~DriverModule();
    DriverModule(const DriverModule &) = delete;
    DriverModule &operator=(const DriverModule &) = delete;

    Result<std::vector<std::pair<DeviceDescriptor, std::string>>> discover();
    Result<VnaDeviceHandle> createDevice(const std::string &driverDeviceId);
    const VnaDriverApiV1 &api() const;

private:
    // 四项资源属于同一模块；析构函数显式先 Destroy 驱动实例，再 dlclose 动态库。
    void *libraryHandle_{nullptr};
    std::string path_;
    VnaDriverApiV1 api_{};
    VnaDriverHandle driverHandle_{nullptr};
    std::mutex moduleMutex_;
};

// 设备适配器把 C ABI 错误、能力和准备结果转换为宿主强类型对象。
class DriverModuleAdapter final {
public:
    DriverModuleAdapter(std::shared_ptr<DriverModule> module, VnaDeviceHandle deviceHandle);
    ~DriverModuleAdapter();
    DriverModuleAdapter(const DriverModuleAdapter &) = delete;
    DriverModuleAdapter &operator=(const DriverModuleAdapter &) = delete;

    Result<VnaCapabilities> connect();
    Result<VnaCapabilities> capabilities();
    Result<PreparedSweep> prepare(const VnaSweepPlan &plan);
    VnaError execute(SweepId sweepId);
    VnaError requestAbort(SweepId sweepId);
    VnaError disconnect();

private:
    // module_ 同时提供函数表和动态库所有权；deviceHandle_ 只能交还给同一模块。
    std::shared_ptr<DriverModule> module_;
    VnaDeviceHandle deviceHandle_{nullptr};
    // connected_ 只表示 Connect 已成功且尚未成功 Disconnect，供析构补偿清理。
    bool connected_{false};
};

VnaError fromDriverStatus(const VnaStatusV1 &status);

}  // namespace vna::detail

#endif
