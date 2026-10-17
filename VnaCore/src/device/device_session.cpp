/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_core/device_session.h"

namespace VnaCore {
namespace Detail {

std::vector<PortFrame> DeviceSession::AcquireSweep(const SweepPlan &plan, const SweepProgressCallback &progress)
{
    static_cast<void>(progress);
    return AcquireSweep(plan);
}

std::vector<PortFrame> DeviceSession::AcquireSweep(
    const SweepPlan &plan, const SweepProgressCallback &progress, const SweepReadGate &beforeRead)
{
    static_cast<void>(beforeRead);
    return AcquireSweep(plan, progress);
}

bool DeviceSession::ManualSweepTimeSupported() const noexcept
{
    return false;
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
