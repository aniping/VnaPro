/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_WEB_CALIBRATION_CSV_READER_H
#define VNAPRO_WEB_CALIBRATION_CSV_READER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VnaWeb {
namespace CalibrationCsvDetail {
// 单行有界读取，不保留全文件的单元格副本；bytes 的生命周期须覆盖 reader。
class Rows final {
public:
    explicit Rows(const std::string &bytes);
    bool Next(std::vector<std::string> &row);

private:
    std::string Cell();
    std::string QuotedCell();
    std::string UnquotedCell();
    const std::string &bytes_;
    std::size_t position_ = 0U;
};

void Require(bool condition);
double Numeric(const std::string &text);
std::uint64_t Identifier(const std::string &text);
}  // 命名空间 CalibrationCsvDetail
}  // 命名空间 VnaWeb
#endif
