#ifndef VNA_HARDWARE_DRIVER_PLUGIN_LOADER_H
#define VNA_HARDWARE_DRIVER_PLUGIN_LOADER_H

#include <memory>
#include <string>
#include <vector>

#include "DriverModuleAdapter.h"

namespace vna::detail {

// 仅返回通过目录权限、文件类型、ABI 表和模块句柄校验的驱动。
class DriverPluginLoader final {
public:
    static Result<std::vector<std::shared_ptr<DriverModule>>> loadDirectory(const std::string &directory);
};

}  // namespace vna::detail

#endif
