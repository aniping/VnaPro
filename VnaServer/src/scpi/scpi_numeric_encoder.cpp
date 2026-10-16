/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_numeric_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

#include "scpi/scpi_constants.h"

namespace VnaScpi {
namespace {
void AppendReal64(std::string &output, double value, ByteOrder order)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
        "SCPI REAL,64 requires IEEE 754 binary64");
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned byte = 0U; byte < sizeof(bits); ++byte) {
        const unsigned shift = (order == ByteOrder::SWAPPED ? byte : 7U - byte) * 8U;
        output += static_cast<char>((bits >> shift) & 0xffU);
    }
}
}  // 匿名命名空间

bool NumericEncoder::Begin(std::vector<double> values, NumericFormat format, ByteOrder order)
{
    Clear();
    if (values.empty() || values.size() > NUMERIC_ARRAY_BYTE_LIMIT / sizeof(double)) {
        return false;
    }
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    format_ = format;
    order_ = order;
    if (format == NumericFormat::REAL64) {
        const auto bytes = std::to_string(values.size() * sizeof(double));
        output_ = "#" + std::to_string(bytes.size()) + bytes;
        output_.reserve(output_.size() + values.size() * sizeof(double));
    }
    values_ = std::move(values);
    return true;
}

bool NumericEncoder::Pending() const noexcept
{
    return !values_.empty();
}

bool NumericEncoder::Poll()
{
    const auto end = (std::min)(position_ + ENCODING_BATCH_SIZE, values_.size());
    std::ostringstream text;
    if (format_ == NumericFormat::ASCII) {
        text.exceptions(std::ios::badbit | std::ios::failbit);
        text.imbue(std::locale::classic());
        text << std::scientific << std::setprecision(ASCII_FRACTIONAL_DIGITS);
    }
    for (; position_ < end; ++position_) {
        if (format_ == NumericFormat::REAL64) {
            AppendReal64(output_, values_[position_], order_);
        } else {
            if (position_ != 0U) {
                text << ',';
            }
            text << values_[position_];
        }
    }
    if (format_ == NumericFormat::ASCII) {
        output_ += text.str();
    }
    if (output_.size() > NUMERIC_ARRAY_BYTE_LIMIT) {
        Clear();
        return false;
    }
    if (position_ == values_.size()) {
        values_.clear();
    }
    return true;
}

std::string NumericEncoder::TakeOutput()
{
    return std::move(output_);
}

void NumericEncoder::Clear() noexcept
{
    values_.clear();
    position_ = 0U;
    output_.clear();
}
}  // 命名空间 VnaScpi
