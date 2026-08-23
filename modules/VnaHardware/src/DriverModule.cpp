#include "DriverModuleAdapter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vna::detail {
namespace {

    // 所有来自插件的计数和字符串都设硬上限，动态库不是可信内存源。
    constexpr std::size_t maximumExternalString = 4096;
    constexpr std::size_t maximumDiscoveredDevices = 1024;

    std::string copyString(VnaStringViewV1 view)
    {
        // 深拷贝切断插件缓冲区生命周期；上限也阻止恶意长度触发巨量读取。
        if ((view.size == 0) || (view.data == nullptr)) {
            return {};
        }
        return std::string(view.data, std::min(view.size, maximumExternalString));
    }

    VnaError localError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    bool validString(VnaStringViewV1 view, bool allowEmpty)
    {
        if (view.size > maximumExternalString) {
            return false;
        }
        if (view.size == 0) {
            return allowEmpty;
        }
        return view.data != nullptr;
    }

    void closeLibrary(void *handle)
    {
        // 该函数只在 DriverModule 析构末尾调用，此时所有驱动对象均已销毁。
#if defined(_WIN32)
        if (handle != nullptr) {
            FreeLibrary(static_cast<HMODULE>(handle));
        }
#else
        if (handle != nullptr) {
            dlclose(handle);
        }
#endif
    }

    struct DiscoveryContext {
        // 回调不能抛异常穿越 C ABI，用 invalid 把错误带回 Discover 的同步调用栈。
        std::vector<std::pair<DeviceDescriptor, std::string>> devices;
        bool invalid{false};
    };

    bool validDiscoveredDevice(const VnaDeviceDescriptorViewV1 *view, std::size_t currentCount)
    {
        // 先验证版本化头部，再读取其余字段，兼容旧结构并避免越界解释内存。
        if ((view == nullptr) || (view->structSize < sizeof(VnaDeviceDescriptorViewV1)) || (view->reserved != 0)) {
            return false;
        }
        const std::array<bool, 6> fields{
            view->portCount > 0,
            validString(view->driverDeviceId, false),
            validString(view->vendor, true),
            validString(view->model, true),
            validString(view->serialNumber, true),
            currentCount < maximumDiscoveredDevices};
        return std::all_of(fields.begin(), fields.end(), [](bool valid) { return valid; });
    }

    void copyDiscoveredDevice(const VnaDeviceDescriptorViewV1 *view, void *contextPointer) noexcept
    {
        try {
            auto &context = *static_cast<DiscoveryContext *>(contextPointer);
            if (!validDiscoveredDevice(view, context.devices.size())) {
                context.invalid = true;
                return;
            }
            // 回调视图在 Discover 返回后失效，不能把驱动内存地址保存到管理器。
            DeviceDescriptor descriptor;
            descriptor.vendor = copyString(view->vendor);
            descriptor.model = copyString(view->model);
            descriptor.serialNumber = copyString(view->serialNumber);
            descriptor.portCount = view->portCount;
            context.devices.emplace_back(std::move(descriptor), copyString(view->driverDeviceId));
        } catch (...) {
            static_cast<DiscoveryContext *>(contextPointer)->invalid = true;
        }
    }

    VnaErrorCode mapDriverCode(std::uint32_t code)
    {
        static constexpr std::array<VnaErrorCode, 13> codes{
            VnaErrorCode::None,           VnaErrorCode::InvalidPlan,
            VnaErrorCode::InvalidState,   VnaErrorCode::UnsupportedPlan,
            VnaErrorCode::DeviceNotFound, VnaErrorCode::ConnectionFailed,
            VnaErrorCode::ConnectionLost, VnaErrorCode::Timeout,
            VnaErrorCode::ProtocolError,  VnaErrorCode::HardwareFault,
            VnaErrorCode::DataLoss,       VnaErrorCode::Cancelled,
            VnaErrorCode::InternalError};
        // 未知状态码视为协议错误，不能把插件整数强转成宿主枚举。
        if (code >= codes.size()) {
            return VnaErrorCode::ProtocolError;
        }
        return codes[code];
    }

}  // namespace

VnaError fromDriverStatus(const VnaStatusV1 &status)
{
    VnaError result = localError(mapDriverCode(status.code), copyString(status.message));
    result.vendorCode = status.vendorCode;
    return result;
}

DriverModule::DriverModule(void *libraryHandle, std::string path, VnaDriverApiV1 api, VnaDriverHandle driverHandle)
    : libraryHandle_(libraryHandle), path_(std::move(path)), api_(api), driverHandle_(driverHandle)
{
}

DriverModule::~DriverModule()
{
    // 严格遵守“先销毁驱动实例、后卸载代码”的顺序，否则 Destroy 函数地址会失效。
    if ((driverHandle_ != nullptr) && (api_.Destroy != nullptr)) {
        api_.Destroy(driverHandle_);
    }
    closeLibrary(libraryHandle_);
}

Result<std::vector<std::pair<DeviceDescriptor, std::string>>> DriverModule::discover()
{
    DiscoveryContext context;
    // V1 驱动实例默认非线程安全，Discover 与 CreateDevice 在模块级互斥量下串行。
    std::lock_guard<std::mutex> lock(moduleMutex_);
    const VnaStatusV1 status = api_.Discover(driverHandle_, &copyDiscoveredDevice, &context);
    if (status.code != VNA_STATUS_OK) {
        return Result<decltype(context.devices)>::failure(fromDriverStatus(status));
    }
    if (context.invalid) {
        return Result<decltype(context.devices)>::failure(
            localError(VnaErrorCode::ProtocolError, "driver returned an invalid device"));
    }
    return Result<decltype(context.devices)>::success(std::move(context.devices));
}

Result<VnaDeviceHandle> DriverModule::createDevice(const std::string &driverDeviceId)
{
    VnaDeviceHandle handle = nullptr;
    const VnaStringViewV1 view{driverDeviceId.data(), driverDeviceId.size()};
    std::lock_guard<std::mutex> lock(moduleMutex_);
    // view 只借用本地 string，驱动必须在同步调用返回前完成读取。
    const VnaStatusV1 status = api_.CreateDevice(driverHandle_, view, &handle);
    if (status.code != VNA_STATUS_OK) {
        return Result<VnaDeviceHandle>::failure(fromDriverStatus(status));
    }
    // “成功 + 空句柄”违反 ABI，显式归为协议错误而不是把空指针传给后续回调。
    if (handle == nullptr) {
        return Result<VnaDeviceHandle>::failure(
            localError(VnaErrorCode::ProtocolError, "driver returned a null device handle"));
    }
    return Result<VnaDeviceHandle>::success(handle);
}

const VnaDriverApiV1 &DriverModule::api() const
{
    return api_;
}

}  // namespace vna::detail
