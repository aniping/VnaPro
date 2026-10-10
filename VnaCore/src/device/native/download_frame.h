/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_DOWNLOAD_FRAME_H
#define VNAPRO_DOWNLOAD_FRAME_H
#include <cstdint>
#include <vector>

namespace VnaCore {
namespace Detail {
struct DownloadFrame final {
    std::vector<std::uint32_t> parameters;
    std::vector<double> table;
};
}  // 命名空间 Detail
}  // 命名空间 VnaCore
#endif
