#include "SimulatorDevice.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <mutex>
#include <string>

namespace vna::simulator {
namespace {

    struct SimulatorDriver {
        VnaDriverHostApiV1 hostApi{};
    };

    std::mutex hostApiMutex;
    VnaDriverHostApiV1 configuredHostApi{};

    void configureHostApi(const VnaDriverHostApiV1 &hostApi)
    {
        // GetApi 与 Create 可能来自不同线程，复制宿主表时用互斥量建立可见性。
        std::lock_guard<std::mutex> lock(hostApiMutex);
        configuredHostApi = hostApi;
    }

    VnaDriverHostApiV1 copyHostApi()
    {
        std::lock_guard<std::mutex> lock(hostApiMutex);
        return configuredHostApi;
    }

    VnaStringViewV1 literalView(const char *text)
    {
        // 仅包装静态字符串字面量，生命周期覆盖动态库的整个装载期。
        return VnaStringViewV1{text, std::char_traits<char>::length(text)};
    }

    bool equalView(VnaStringViewV1 view, const char *text)
    {
        const std::size_t length = std::char_traits<char>::length(text);
        return (view.data != nullptr) && (view.size == length) && (std::memcmp(view.data, text, length) == 0);
    }

    VnaStatusV1 createDriver(const VnaDriverCreateInfoV1 *info, VnaDriverHandle *handle) noexcept
    {
        try {
            // C ABI 不允许异常外逸；同时尊重 structSize，拒绝读取旧调用方不存在的字段。
            if ((info == nullptr) || (info->structSize < sizeof(*info)) || (handle == nullptr)) {
                return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator create request");
            }
            auto *driver = new SimulatorDriver();
            driver->hostApi = copyHostApi();
            *handle = driver;
            return simulatorStatus(VNA_STATUS_OK, "");
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator creation failed");
        }
    }

    void destroyDriver(VnaDriverHandle handle) noexcept
    {
        delete static_cast<SimulatorDriver *>(handle);
    }

    VnaStatusV1 discoverDevices(VnaDriverHandle handle, VnaDeviceFoundCallbackV1 callback, void *context) noexcept
    {
        try {
            if ((handle == nullptr) || (callback == nullptr)) {
                return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator discovery");
            }
            // 描述符和字符串仅在 callback 返回前有效，宿主负责同步深拷贝。
            VnaDeviceDescriptorViewV1 descriptor{};
            descriptor.structSize = static_cast<std::uint32_t>(sizeof(descriptor));
            descriptor.portCount = 4;
            descriptor.driverDeviceId = literalView("simulator-0");
            descriptor.vendor = literalView("VnaPro");
            descriptor.model = literalView("Deterministic four-port simulator");
            descriptor.serialNumber = literalView("SIM-0001");
            callback(&descriptor, context);
            return simulatorStatus(VNA_STATUS_OK, "");
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator discovery callback failed");
        }
    }

    VnaStatusV1 createDevice(VnaDriverHandle handle, VnaStringViewV1 identifier, VnaDeviceHandle *device) noexcept
    {
        try {
            if ((handle == nullptr) || (device == nullptr) || !equalView(identifier, "simulator-0")) {
                return simulatorStatus(VNA_STATUS_NOT_FOUND, "simulator device was not found");
            }
            // 每个设备保存一份 hostApi，避免依赖之后可能被另一轮 GetApi 更新的全局副本。
            const auto *driver = static_cast<SimulatorDriver *>(handle);
            *device = new SimulatorDevice(driver->hostApi);
            return simulatorStatus(VNA_STATUS_OK, "");
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator device creation failed");
        }
    }

    void destroyDevice(VnaDeviceHandle handle) noexcept
    {
        delete static_cast<SimulatorDevice *>(handle);
    }

    VnaStatusV1 connectDevice(VnaDeviceHandle handle) noexcept
    {
        try {
            return handle == nullptr ? simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "simulator handle is null")
                                     : static_cast<SimulatorDevice *>(handle)->connect();
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator connect failed");
        }
    }

    VnaStatusV1 disconnectDevice(VnaDeviceHandle handle) noexcept
    {
        try {
            return handle == nullptr ? simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "simulator handle is null")
                                     : static_cast<SimulatorDevice *>(handle)->disconnect();
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator disconnect failed");
        }
    }

    VnaStatusV1 getCapabilities(VnaDeviceHandle handle, VnaCapabilitiesViewV1 *view) noexcept
    {
        try {
            return handle == nullptr ? simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "simulator handle is null")
                                     : static_cast<SimulatorDevice *>(handle)->capabilities(view);
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator capabilities failed");
        }
    }

    VnaStatusV1 prepareDevice(
        VnaDeviceHandle handle, const VnaSweepPlanViewV1 *plan, VnaPrepareResultViewV1 *result) noexcept
    {
        try {
            return handle == nullptr ? simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "simulator handle is null")
                                     : static_cast<SimulatorDevice *>(handle)->prepare(plan, result);
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator prepare failed");
        }
    }

    VnaStatusV1 executeDevice(
        VnaDeviceHandle handle, VnaSweepIdV1 sweepId, const VnaExecutionContextV1 *context) noexcept
    {
        try {
            if ((handle == nullptr) || (context == nullptr) || (context->structSize < sizeof(*context))) {
                return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator execution");
            }
            // V1 execution context 暂无额外字段，但仍校验版本化头部，为 ABI 扩展留出边界。
            return static_cast<SimulatorDevice *>(handle)->execute(sweepId);
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator execution failed");
        }
    }

    VnaStatusV1 abortDevice(VnaDeviceHandle handle, VnaSweepIdV1 sweepId) noexcept
    {
        try {
            return handle == nullptr ? simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "simulator handle is null")
                                     : static_cast<SimulatorDevice *>(handle)->requestAbort(sweepId);
        } catch (...) {
            return simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator abort failed");
        }
    }

    VnaDriverApiV1 makeApi()
    {
        // 返回表完全按值构造；除字符串字面量和函数地址外不暴露驱动内部对象。
        VnaDriverApiV1 api{};
        api.structSize = static_cast<std::uint32_t>(sizeof(api));
        api.abiMajor = VNA_DRIVER_ABI_MAJOR_V1;
        api.abiMinor = VNA_DRIVER_ABI_MINOR_V1;
        api.metadata.structSize = static_cast<std::uint32_t>(sizeof(api.metadata));
        api.metadata.driverId = literalView("vnapro.simulator");
        api.metadata.displayName = literalView("VnaPro deterministic simulator");
        api.Create = &createDriver;
        api.Destroy = &destroyDriver;
        api.Discover = &discoverDevices;
        api.CreateDevice = &createDevice;
        api.DestroyDevice = &destroyDevice;
        api.Connect = &connectDevice;
        api.Disconnect = &disconnectDevice;
        api.GetCapabilities = &getCapabilities;
        api.Prepare = &prepareDevice;
        api.Execute = &executeDevice;
        api.RequestAbort = &abortDevice;
        return api;
    }

    bool hasRequiredHostCallbacks(const VnaDriverHostApiV1 &hostApi)
    {
        // 模拟器执行依赖完整的取消与缓冲区协议，缺任一回调都不能安全工作。
        return (hostApi.Log != nullptr) && (hostApi.IsCancellationRequested != nullptr) &&
               (hostApi.AcquireWaveBlock != nullptr) && (hostApi.CommitWaveBlock != nullptr) &&
               (hostApi.DiscardWaveBlock != nullptr);
    }

    bool validApiRequest(
        std::uint32_t requestedVersion, const VnaDriverHostApiV1 *hostApi, const VnaDriverApiV1 *driverApi)
    {
        const bool validPointers = (hostApi != nullptr) && (driverApi != nullptr);
        if (!validPointers) {
            return false;
        }
        const std::array<bool, 5> requirements{
            requestedVersion == VNA_DRIVER_ABI_VERSION_V1, hostApi->structSize >= sizeof(*hostApi),
            driverApi->structSize >= sizeof(*driverApi), hostApi->abiMajor == VNA_DRIVER_ABI_MAJOR_V1,
            hasRequiredHostCallbacks(*hostApi)};
        return std::all_of(requirements.begin(), requirements.end(), [](bool valid) { return valid; });
    }

}  // namespace
}  // namespace vna::simulator

extern "C" VnaStatusV1 VnaDriverGetApi(
    std::uint32_t requestedVersion, const VnaDriverHostApiV1 *hostApi, VnaDriverApiV1 *driverApi)
{
    try {
        // 这是动态库唯一导出入口，先双向校验 ABI 再发布任何函数指针。
        if (!vna::simulator::validApiRequest(requestedVersion, hostApi, driverApi)) {
            return vna::simulator::simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "simulator ABI request is invalid");
        }
        vna::simulator::configureHostApi(*hostApi);
        *driverApi = vna::simulator::makeApi();
        return vna::simulator::simulatorStatus(VNA_STATUS_OK, "");
    } catch (...) {
        return vna::simulator::simulatorStatus(VNA_STATUS_INTERNAL_ERROR, "simulator ABI initialization failed");
    }
}
