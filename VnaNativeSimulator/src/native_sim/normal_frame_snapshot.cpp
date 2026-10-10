/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "normal_frame_snapshot.h"

#include <utility>

namespace vna_native {

void NormalFrameSnapshot::Swap(NormalFrameSnapshot &other) noexcept
{
    std::swap(sourcePort, other.sourcePort);
    points.swap(other.points);
    tableData.swap(other.tableData);
}

}  // namespace vna_native
