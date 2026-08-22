#ifndef VNA_HARDWARE_DRIVER_RESULT_VALIDATION_H
#define VNA_HARDWARE_DRIVER_RESULT_VALIDATION_H

#include <vector>

#include "Result.h"
#include "VnaCapabilities.h"
#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaSweepPlan.h"

namespace vna::detail {

// ABI 视图均来自不可信插件；复制前验证边界，复制后再验证与请求计划的一致性。
// copy* 负责切断插件内存生命周期，validate* 负责协议和计划语义，不可省略任一阶段。
VnaCapabilities copyDriverCapabilities(const VnaCapabilitiesViewV1 &view);
// abortCallbackAvailable 用于交叉核对能力位，避免插件声明功能却未提供入口。
bool driverCapabilitiesAreValid(const VnaCapabilitiesViewV1 &view, bool abortCallbackAvailable);
Result<std::vector<VnaWaveLayout>> copyDriverLayouts(const VnaPrepareResultViewV1 &view);
VnaError validateDriverLayouts(const std::vector<VnaWaveLayout> &layouts, const VnaSweepPlan &plan);
// 执行报告必须满足用户策略；V1 无实际量化值通道，因此量化报告固定失败关闭。
VnaError validateDriverExecutionReport(const VnaPrepareResultViewV1 &view, const SweepPolicy &policy);

}  // namespace vna::detail

#endif
