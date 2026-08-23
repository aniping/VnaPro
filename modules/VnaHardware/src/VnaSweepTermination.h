#ifndef VNA_HARDWARE_VNA_SWEEP_TERMINATION_H
#define VNA_HARDWARE_VNA_SWEEP_TERMINATION_H

#include <cstddef>

#include "VnaError.h"
#include "VnaHardwareEvent.h"

namespace vna::detail {

// None 是驱动已接受；InvalidState 还可能是 Start 事件与 Execute 入口之间的交接窗口。
// 两者都不能立即结束中止 CommandId，必须由扫频的实际终态最终裁决。
bool abortRequestAwaitsSweep(VnaErrorCode requestError);

// 合并 Worker、数据通路和覆盖检查结果，并保留最具体的故障终因。
// RequestAbort 返回 OK 只表示请求已被接收；只有 Execute 返回 Cancelled 才证明扫频真正中止。
SweepTerminationReason normalizeSweepTermination(
    VnaErrorCode workerError, VnaErrorCode recordedError, std::size_t received, std::size_t expected);

}  // namespace vna::detail

#endif
