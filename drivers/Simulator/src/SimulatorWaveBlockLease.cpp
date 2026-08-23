#include "SimulatorWaveBlockLease.h"

namespace vna::simulator {

SimulatorWaveBlockLease::SimulatorWaveBlockLease(const VnaDriverHostApiV1 &hostApi, VnaBufferHandle handle) noexcept
    : hostApi_(hostApi), handle_(handle)
{
}

SimulatorWaveBlockLease::~SimulatorWaveBlockLease() noexcept
{
    if (!active_) {
        return;
    }
    try {
        hostApi_.DiscardWaveBlock(handle_);
    } catch (...) {
        // C ABI 回调不得抛异常；析构清理仍不能让异常逃逸或覆盖原始故障。
    }
}

VnaStatusV1 SimulatorWaveBlockLease::commit(std::uint32_t firstPoint, std::uint32_t pointCount)
{
    const VnaStatusV1 status = hostApi_.CommitWaveBlock(handle_, firstPoint, pointCount);
    // Commit 正常返回即完成句柄交接；status 只说明数据是否发布成功，不能据此二次 Discard。
    active_ = false;
    return status;
}

}  // namespace vna::simulator
