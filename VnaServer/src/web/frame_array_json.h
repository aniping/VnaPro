/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_FRAME_ARRAY_JSON_H
#define VNAPRO_WEB_FRAME_ARRAY_JSON_H

#include <cstddef>
#include <functional>

#include "web/json.h"

namespace VnaWeb {

struct FrameArrayNumber final {
    double value = 0.0;
    bool present = false;
};

// 只改变完整帧数组的传输表示，不抽点、不重算测量或投影结果。
class FrameArrayJson final {
public:
    FrameArrayJson() = delete;
    // 数值/null 使用 f64le，布尔使用 u8，最多 256 种字符串使用 enum8。
    // 对象和混合数组递归处理，空数组保留；拒绝数值数组中的非有限数。
    static Json Pack(Json value);
    // 直接读取完整数值/状态序列，packed 时不创建逐点 JSON 节点；空序列仍为 []。
    // reader 仅在调用期间使用；普通表示是默认值，数值的 present=false 表示 null。
    static Json Numbers(
        std::size_t count, const std::function<FrameArrayNumber(std::size_t)> &reader, bool packed = false);
    static Json Booleans(std::size_t count, const std::function<bool(std::size_t)> &reader, bool packed = false);
    static Json Strings(std::size_t count, const std::function<std::string(std::size_t)> &reader, bool packed = false);
};

}  // 命名空间 VnaWeb

#endif
