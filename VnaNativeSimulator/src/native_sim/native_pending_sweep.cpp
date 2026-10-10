/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_pending_sweep.h"

#include <utility>

namespace vna_native {

void NativePendingSweep::Swap(NativePendingSweep &other) noexcept
{
    std::swap(valid, other.valid);
    std::swap(pointCount, other.pointCount);
    std::swap(sourcePort, other.sourcePort);
    iqData.swap(other.iqData);
}

}  // namespace vna_native
