#include "DriverModuleAdapter.h"

#include <cstdint>
#include <string>
#include <utility>

#include "DriverResultValidation.h"
#include "VnaPlanMarshaller.h"

namespace vna::detail {
namespace {

    VnaError localError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    PreparedSweep makePreparedSweep(
        const VnaSweepPlan &plan, const VnaPrepareResultViewV1 &view, std::vector<VnaWaveLayout> layouts)
    {
        // 只有完成执行报告与布局校验后才进入这里，将驱动结果整理为稳定的领域对象。
        PreparedSweep prepared;
        prepared.effectivePlan = plan;
        prepared.layouts = std::move(layouts);
        prepared.report.executionMode =
            (view.executionFlags & VNA_EXECUTION_COMPILED) != 0 ? ExecutionMode::Compiled : ExecutionMode::Native;
        prepared.report.physicalSweepCount = view.physicalSweepCount;
        prepared.report.preferredBatchPoints = view.preferredBatchPoints;
        prepared.report.estimatedBytesPerPoint = view.estimatedBytesPerPoint;
        prepared.report.quantized = (view.executionFlags & VNA_EXECUTION_QUANTIZED) != 0;
        prepared.report.atomic = (view.executionFlags & VNA_EXECUTION_ATOMIC) != 0;
        prepared.report.phaseCoherent = (view.executionFlags & VNA_EXECUTION_PHASE_COHERENT) != 0;
        return prepared;
    }

}  // namespace

DriverModuleAdapter::DriverModuleAdapter(std::shared_ptr<DriverModule> module, VnaDeviceHandle deviceHandle)
    : module_(std::move(module)), deviceHandle_(deviceHandle)
{
}

DriverModuleAdapter::~DriverModuleAdapter()
{
    // 设备句柄必须在模块 shared_ptr 释放前销毁，确保调用的函数地址仍属于已加载动态库。
    if (connected_) {
        module_->api().Disconnect(deviceHandle_);
    }
    module_->api().DestroyDevice(deviceHandle_);
}

Result<VnaCapabilities> DriverModuleAdapter::connect()
{
    const VnaStatusV1 status = module_->api().Connect(deviceHandle_);
    if (status.code != VNA_STATUS_OK) {
        return Result<VnaCapabilities>::failure(fromDriverStatus(status));
    }
    // Connect 成功但能力查询失败仍不是可用连接，立即补偿 Disconnect 回到一致状态。
    connected_ = true;
    auto result = capabilities();
    if (!result.hasValue()) {
        module_->api().Disconnect(deviceHandle_);
        connected_ = false;
    }
    return result;
}

Result<VnaCapabilities> DriverModuleAdapter::capabilities()
{
    // structSize 同时声明宿主缓冲区大小，并允许未来驱动按版本决定可写字段。
    VnaCapabilitiesViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));
    const VnaStatusV1 status = module_->api().GetCapabilities(deviceHandle_, &view);
    if (status.code != VNA_STATUS_OK) {
        return Result<VnaCapabilities>::failure(fromDriverStatus(status));
    }
    // 不直接信任动态库填充的数值和能力位，所有跨边界结果都经宿主验证。
    if (!driverCapabilitiesAreValid(view, module_->api().RequestAbort != nullptr)) {
        return Result<VnaCapabilities>::failure(
            localError(VnaErrorCode::ProtocolError, "driver returned invalid capabilities"));
    }
    return Result<VnaCapabilities>::success(copyDriverCapabilities(view));
}

Result<PreparedSweep> DriverModuleAdapter::prepare(const VnaSweepPlan &plan)
{
    // marshaller 持有 ABI 数组；其生命周期必须覆盖整个同步 Prepare 调用。
    VnaPlanMarshaller marshaller(plan);
    VnaPrepareResultViewV1 view{};
    view.structSize = static_cast<std::uint32_t>(sizeof(view));
    const VnaStatusV1 status = module_->api().Prepare(deviceHandle_, &marshaller.view(), &view);
    if (status.code != VNA_STATUS_OK) {
        return Result<PreparedSweep>::failure(fromDriverStatus(status));
    }
    // 先校验标量执行报告，再访问由驱动借出的布局指针，缩小不可信数据面。
    const VnaError reportError = validateDriverExecutionReport(view, plan.policy);
    if (reportError.code != VnaErrorCode::None) {
        return Result<PreparedSweep>::failure(reportError);
    }
    auto layouts = copyDriverLayouts(view);
    if (!layouts.hasValue()) {
        return Result<PreparedSweep>::failure(layouts.error());
    }
    const VnaError layoutError = validateDriverLayouts(layouts.value(), plan);
    if (layoutError.code != VnaErrorCode::None) {
        return Result<PreparedSweep>::failure(layoutError);
    }
    // 所有视图已深拷贝完毕，返回后驱动可安全复用 Prepare 的临时存储。
    PreparedSweep prepared = makePreparedSweep(plan, view, layouts.takeValue());
    return Result<PreparedSweep>::success(std::move(prepared));
}

VnaError DriverModuleAdapter::execute(SweepId sweepId)
{
    VnaExecutionContextV1 context{};
    context.structSize = static_cast<std::uint32_t>(sizeof(context));
    return fromDriverStatus(module_->api().Execute(deviceHandle_, sweepId, &context));
}

VnaError DriverModuleAdapter::requestAbort(SweepId sweepId)
{
    // 能力声明已在连接时与函数指针交叉校验，这里仍防御空指针以保护独立调用路径。
    if (module_->api().RequestAbort == nullptr) {
        return localError(VnaErrorCode::UnsupportedPlan, "driver does not support abort");
    }
    return fromDriverStatus(module_->api().RequestAbort(deviceHandle_, sweepId));
}

VnaError DriverModuleAdapter::disconnect()
{
    if (!connected_) {
        return {};
    }
    // 驱动断开失败时保留 connected_，析构仍会再尝试一次清理。
    const VnaError result = fromDriverStatus(module_->api().Disconnect(deviceHandle_));
    if (result.code == VnaErrorCode::None) {
        connected_ = false;
    }
    return result;
}

}  // namespace vna::detail
