/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_FRAME_ARRAY_JSON_H
#define VNAPRO_WEB_FRAME_ARRAY_JSON_H

#include "web/json.h"

namespace VnaWeb {

// 只改变完整帧数组的传输表示，不抽点、不重算测量或投影结果。
class FrameArrayJson final {
public:
    FrameArrayJson() = delete;
    // 数值/null 使用 f64le，布尔使用 u8，最多 256 种字符串使用 enum8。
    // 对象和混合数组递归处理，空数组保留；拒绝数值数组中的非有限数。
    static Json Pack(Json value);
};

}  // 命名空间 VnaWeb

#endif
