#include "DriverPluginLoader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <dlfcn.h>

#include "HostServices.h"
#include "PosixDriverFile.h"
#include "VnaLogger.h"

namespace vna::detail {
namespace {

    VnaError loadError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    bool validExternalString(VnaStringViewV1 value, bool allowEmpty)
    {
        // 元数据来自尚未信任的模块，只检查边界，不在校验前构造 std::string。
        if (value.size > 256) {
            return false;
        }
        if (value.size == 0) {
            return allowEmpty;
        }
        return value.data != nullptr;
    }

    bool hasValidMetadata(const VnaDriverMetadataV1 &metadata)
    {
        if (metadata.structSize < sizeof(VnaDriverMetadataV1)) {
            return false;
        }
        if (!validExternalString(metadata.driverId, false)) {
            return false;
        }
        return validExternalString(metadata.displayName, true);
    }

    bool hasModuleCallbacks(const VnaDriverApiV1 &api)
    {
        return (api.Create != nullptr) && (api.Destroy != nullptr) && (api.Discover != nullptr);
    }

    bool hasDeviceCallbacks(const VnaDriverApiV1 &api)
    {
        return (api.CreateDevice != nullptr) && (api.DestroyDevice != nullptr) && (api.Connect != nullptr) &&
               (api.Disconnect != nullptr);
    }

    bool hasAcquisitionCallbacks(const VnaDriverApiV1 &api)
    {
        return (api.GetCapabilities != nullptr) && (api.Prepare != nullptr) && (api.Execute != nullptr);
    }

    bool hasRequiredApi(const VnaDriverApiV1 &api)
    {
        // 主版本必须完全一致；次版本通过 structSize 和非空回调实现向后兼容。
        const bool validVersion =
            (api.structSize >= sizeof(VnaDriverApiV1)) && (api.abiMajor == VNA_DRIVER_ABI_MAJOR_V1);
        const std::array<bool, 5> requirements{
            validVersion, hasValidMetadata(api.metadata), hasModuleCallbacks(api), hasDeviceCallbacks(api),
            hasAcquisitionCallbacks(api)};
        return std::all_of(requirements.begin(), requirements.end(), [](bool present) { return present; });
    }

    Result<VnaDriverApiV1> queryDriverApi(void *library)
    {
        // RTLD_LOCAL 限制符号外泄，此处再只查固定入口，避免依赖插件其他导出符号。
        const auto getApi = reinterpret_cast<VnaDriverGetApiFunctionV1>(dlsym(library, "VnaDriverGetApi"));
        if (getApi == nullptr) {
            return Result<VnaDriverApiV1>::failure(
                loadError(VnaErrorCode::DriverLoadFailed, "driver entry point is missing"));
        }
        VnaDriverApiV1 api{};
        api.structSize = static_cast<std::uint32_t>(sizeof(api));
        // 宿主表为进程期静态对象，驱动可在模块生命周期内安全保存其地址。
        const VnaStatusV1 status = getApi(VNA_DRIVER_ABI_VERSION_V1, &HostServices::api(), &api);
        if (status.code != VNA_STATUS_OK) {
            return Result<VnaDriverApiV1>::failure(
                loadError(VnaErrorCode::DriverAbiMismatch, "driver rejected the host ABI"));
        }
        if (!hasRequiredApi(api)) {
            return Result<VnaDriverApiV1>::failure(
                loadError(VnaErrorCode::DriverAbiMismatch, "driver API table is incomplete"));
        }
        return Result<VnaDriverApiV1>::success(api);
    }

    Result<VnaDriverHandle> createDriverHandle(const VnaDriverApiV1 &api)
    {
        // 先清零并声明结构大小，未来字段在旧宿主下保持确定的零值。
        VnaDriverCreateInfoV1 createInfo{};
        createInfo.structSize = static_cast<std::uint32_t>(sizeof(createInfo));
        VnaDriverHandle driver = nullptr;
        const VnaStatusV1 status = api.Create(&createInfo, &driver);
        if (status.code != VNA_STATUS_OK) {
            return Result<VnaDriverHandle>::failure(fromDriverStatus(status));
        }
        if (driver == nullptr) {
            return Result<VnaDriverHandle>::failure(
                loadError(VnaErrorCode::ProtocolError, "driver returned a null module handle"));
        }
        return Result<VnaDriverHandle>::success(driver);
    }

    class PendingDriverHandle final {
    public:
        // DriverModule 构造成功前临时拥有句柄，异常时自动调用插件 Destroy。
        PendingDriverHandle(const VnaDriverApiV1 &api, VnaDriverHandle handle) : api_(api), handle_(handle)
        {
        }

        ~PendingDriverHandle()
        {
            if (handle_ != nullptr) {
                api_.Destroy(handle_);
            }
        }

        PendingDriverHandle(const PendingDriverHandle &) = delete;
        PendingDriverHandle &operator=(const PendingDriverHandle &) = delete;

        VnaDriverHandle get() const
        {
            return handle_;
        }

        void release()
        {
            handle_ = nullptr;
        }

    private:
        const VnaDriverApiV1 &api_;
        VnaDriverHandle handle_{nullptr};
    };

    Result<std::shared_ptr<DriverModule>> loadCandidate(int rootDescriptor, const std::string &name)
    {
        // 每一步均返回拥有资源的 RAII 对象，任何校验失败都会按逆序清理。
        auto library = openTrustedDriverFile(rootDescriptor, name);
        if (!library.hasValue()) {
            return Result<std::shared_ptr<DriverModule>>::failure(library.error());
        }
        auto api = queryDriverApi(library.value().get());
        if (!api.hasValue()) {
            return Result<std::shared_ptr<DriverModule>>::failure(api.error());
        }
        auto driver = createDriverHandle(api.value());
        if (!driver.hasValue()) {
            return Result<std::shared_ptr<DriverModule>>::failure(driver.error());
        }
        // make_shared 可能抛出；构造成功前由局部 RAII 同时守住模块句柄和动态库。
        PendingDriverHandle pending(api.value(), driver.takeValue());
        auto module = std::make_shared<DriverModule>(library.value().get(), name, api.value(), pending.get());
        library.value().release();
        pending.release();
        return Result<std::shared_ptr<DriverModule>>::success(std::move(module));
    }

    std::vector<std::shared_ptr<DriverModule>> loadCandidates(int rootDescriptor, const std::vector<std::string> &names)
    {
        std::vector<std::shared_ptr<DriverModule>> modules;
        // 单个插件损坏不阻止其他可信插件工作；只有最终一个都没有才整体失败。
        for (const auto &name : names) {
            auto module = loadCandidate(rootDescriptor, name);
            if (module.hasValue()) {
                VnaLogger::write(VnaLogLevel::Info, "驱动插件加载成功：" + name);
                modules.push_back(module.takeValue());
            } else {
                VnaLogger::write(
                    VnaLogLevel::Warning, "驱动插件被拒绝：" + name + "，原因=" + module.error().description);
            }
        }
        return modules;
    }

}  // namespace

Result<std::vector<std::shared_ptr<DriverModule>>> DriverPluginLoader::loadDirectory(const std::string &directory)
{
    VnaLogger::write(VnaLogLevel::Debug, "开始扫描驱动目录：" + directory);
    // 目录描述符一直存活到所有插件完成加载，防止枚举结果在 dlopen 前被路径替换。
    auto root = openTrustedDriverDirectory(directory);
    if (!root.hasValue()) {
        return Result<std::vector<std::shared_ptr<DriverModule>>>::failure(root.error());
    }
    // 枚举结果已排序，插件加载次序与文件系统 readdir 返回顺序无关。
    auto names = collectTrustedDriverNames(root.value().get());
    if (!names.hasValue()) {
        return Result<std::vector<std::shared_ptr<DriverModule>>>::failure(names.error());
    }
    auto modules = loadCandidates(root.value().get(), names.value());
    if (modules.empty()) {
        return Result<std::vector<std::shared_ptr<DriverModule>>>::failure(
            loadError(VnaErrorCode::DriverLoadFailed, "no valid VNA driver was found"));
    }
    return Result<std::vector<std::shared_ptr<DriverModule>>>::success(std::move(modules));
}

}  // namespace vna::detail
