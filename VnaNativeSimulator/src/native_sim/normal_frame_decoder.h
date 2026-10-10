/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
#define VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H

#include <cstdint>

#include "normal_frame_snapshot.h"

namespace VnaNative {
namespace NormalFrameDecoder {

// 验证指针和声明的元素数量，不读取任一借用缓冲区。
// 元素数量必须与帧布局完全一致；实际容量仍由调用者保证。
VNA_NATIVE_HIDDEN std::uint32_t ValidateInputShape(const NormalFrameInput &input) noexcept;

// 成功时以深拷贝替换输出；失败时保持输出不变。
// 先检查全部源端口，再检查不支持的模式标志；同步由调用者保证。
VNA_NATIVE_HIDDEN std::uint32_t Decode(const NormalFrameInput &input, NormalFrameSnapshot &output) noexcept;

}  // 命名空间 NormalFrameDecoder
}  // 命名空间 VnaNative
#endif  // VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
