/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_pending_sweep.h"

#include <utility>

namespace VnaNative {

void NativePendingSweep::Swap(NativePendingSweep &other) noexcept
{
    std::swap(valid, other.valid);
    std::swap(pointCount, other.pointCount);
    std::swap(sourcePort, other.sourcePort);
    iqData.swap(other.iqData);
}

}  // 命名空间 VnaNative
