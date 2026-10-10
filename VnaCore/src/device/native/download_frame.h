/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_DOWNLOAD_FRAME_H
#define VNAPRO_DOWNLOAD_FRAME_H
#include <cstdint>
#include <vector>

namespace vna_core {
namespace detail {
struct DownloadFrame final {
    std::vector<std::uint32_t> parameters;
    std::vector<double> table;
};
}  // namespace detail
}  // namespace vna_core
#endif
