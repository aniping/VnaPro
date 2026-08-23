#ifndef VNA_HARDWARE_VNA_ERROR_H
#define VNA_HARDWARE_VNA_ERROR_H

#include <cstdint>
#include <string>

namespace vna {

using CommandId = std::uint64_t;
using ConfigurationId = std::uint64_t;
using DiscoveryId = std::uint64_t;
using LayoutId = std::uint32_t;
using SweepId = std::uint64_t;

// 错误码按调用方动作分类：参数/状态错误可修正，连接与硬件错误通常需要重新连接。
enum class VnaErrorCode : std::uint32_t {
    None,
    InvalidState,
    InvalidPlan,
    UnsupportedPlan,
    ConfigurationRejected,
    ConfigurationExpired,
    DeviceNotFound,
    ConnectionFailed,
    ConnectionLost,
    Timeout,
    ProtocolError,
    HardwareFault,
    DataLoss,
    Cancelled,
    DriverLoadFailed,
    DriverAbiMismatch,
    InternalError,
    ResourceExhausted
};

// vendorCode 保留厂商原始诊断；code 是上层可稳定处理的统一错误分类。
struct VnaError {
    // deviceId、commandId、sweepId 在相应上下文不存在时保持零或空。
    VnaErrorCode code{VnaErrorCode::None};
    std::string deviceId;
    CommandId commandId{0};
    SweepId sweepId{0};
    std::int32_t vendorCode{0};
    // description 面向诊断，不作为程序分支条件。
    std::string description;
};

}  // namespace vna

#endif
