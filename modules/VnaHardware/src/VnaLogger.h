#ifndef VNA_HARDWARE_VNA_LOGGER_H
#define VNA_HARDWARE_VNA_LOGGER_H

#include <cstdint>
#include <memory>
#include <string>

#include "Result.h"
#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaLogConfiguration.h"

namespace vna::detail {

// 进程内 Host API 没有日志上下文，因此所有管理器共享一份等价日志配置。
class VnaLogger final {
public:
    static Result<std::shared_ptr<VnaLogger>> acquire(const std::string &configurationFile);
    static void write(VnaLogLevel level, const std::string &message) noexcept;
    static void writeDriver(std::uint32_t level, VnaStringViewV1 message) noexcept;

    ~VnaLogger();
    VnaLogger(const VnaLogger &) = delete;
    VnaLogger &operator=(const VnaLogger &) = delete;

private:
    class Impl;
    explicit VnaLogger(std::unique_ptr<Impl> implementation);
    bool uses(const VnaLogConfiguration &configuration) const;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace vna::detail

#endif
