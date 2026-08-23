#include "VnaHardwareManager.h"

#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "DriverPluginLoader.h"
#include "VnaConfigurationFile.h"
#include "VnaHardwareManagerImpl.h"
#include "VnaHardwareResourceLimits.h"
#include "VnaLogConfiguration.h"
#include "VnaLogger.h"

namespace vna {
namespace {

    VnaError creationError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    std::string defaultConfigurationFile(const std::string &driverDirectory)
    {
        if (driverDirectory.empty()) {
            return "vnapro.conf";
        }
        const bool hasSeparator = driverDirectory.back() == '/';
        return driverDirectory + (hasSeparator ? "vnapro.conf" : "/vnapro.conf");
    }

}  // namespace

Result<std::unique_ptr<VnaHardwareManager>> VnaHardwareManager::create(std::string driverDirectory)
{
    try {
        return create(driverDirectory, defaultConfigurationFile(driverDirectory));
    } catch (const std::bad_alloc &) {
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(
            creationError(VnaErrorCode::ResourceExhausted, "hardware manager allocation failed"));
    } catch (const std::exception &error) {
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(
            creationError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(
            creationError(VnaErrorCode::InternalError, "hardware manager creation failed"));
    }
}

Result<std::unique_ptr<VnaHardwareManager>> VnaHardwareManager::create(
    std::string driverDirectory, std::string configurationFile)
{
    try {
        // 日志与资源策略来自同一次受信文件读取，配置原子替换不能混出跨代 Manager。
        auto trustedConfiguration = detail::readTrustedConfigurationFile(configurationFile);
        if (!trustedConfiguration.hasValue()) {
            return Result<std::unique_ptr<VnaHardwareManager>>::failure(trustedConfiguration.error());
        }
        auto limits = detail::loadVnaHardwareResourceLimits(trustedConfiguration.value());
        if (!limits.hasValue()) {
            return Result<std::unique_ptr<VnaHardwareManager>>::failure(limits.error());
        }
        auto logConfiguration = detail::loadVnaLogConfiguration(trustedConfiguration.value());
        if (!logConfiguration.hasValue()) {
            return Result<std::unique_ptr<VnaHardwareManager>>::failure(logConfiguration.error());
        }
        auto logger = detail::VnaLogger::acquire(logConfiguration.takeValue());
        if (!logger.hasValue()) {
            return Result<std::unique_ptr<VnaHardwareManager>>::failure(logger.error());
        }
        detail::VnaLogger::write(detail::VnaLogLevel::Info, "正在创建硬件管理器，驱动目录=" + driverDirectory);
        auto modules = detail::DriverPluginLoader::loadDirectory(driverDirectory);
        if (!modules.hasValue()) {
            detail::VnaLogger::write(detail::VnaLogLevel::Error, "驱动模块加载失败：" + modules.error().description);
            return Result<std::unique_ptr<VnaHardwareManager>>::failure(modules.error());
        }
        auto implementation = std::make_unique<Impl>(logger.takeValue(), modules.takeValue(), limits.takeValue());
        auto manager = std::unique_ptr<VnaHardwareManager>(new VnaHardwareManager(std::move(implementation)));
        detail::VnaLogger::write(detail::VnaLogLevel::Info, "硬件管理器创建完成");
        return Result<std::unique_ptr<VnaHardwareManager>>::success(std::move(manager));
    } catch (const std::bad_alloc &) {
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(
            creationError(VnaErrorCode::ResourceExhausted, "hardware manager allocation failed"));
    } catch (const std::exception &error) {
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(
            creationError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(
            creationError(VnaErrorCode::InternalError, "hardware manager creation failed"));
    }
}

}  // namespace vna
