/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_NUMERIC_ENCODER_H
#define VNAPRO_SCPI_NUMERIC_ENCODER_H

#include <cstddef>
#include <string>
#include <vector>

namespace VnaScpi {
enum class NumericFormat {
    ASCII,
    REAL64
};

enum class ByteOrder {
    NORMAL,
    SWAPPED
};

// 冻结本次响应的格式和字节序，按有界批次编码；输出不含消息末尾 LF。
class NumericEncoder final {
public:
    bool Begin(std::vector<double> values, NumericFormat format, ByteOrder order);
    bool Pending() const noexcept;
    // 达到预算时返回假并丢弃全部缓冲，禁止发送不完整的二进制块。
    bool Poll();
    std::string TakeOutput();
    void Clear() noexcept;

private:
    std::vector<double> values_;
    std::size_t position_ = 0U;
    std::string output_;
    NumericFormat format_ = NumericFormat::ASCII;
    ByteOrder order_ = ByteOrder::NORMAL;
};
}  // 命名空间 VnaScpi
#endif
