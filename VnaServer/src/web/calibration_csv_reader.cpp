/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_csv_reader.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "web/calibration_csv.h"
#include "web/json_parser.h"

namespace VnaWeb {
namespace CalibrationCsvDetail {
void Require(bool condition)
{
    if (!condition) {
        throw std::invalid_argument("Invalid calibration CSV structure, metadata or numeric data");
    }
}

Rows::Rows(const std::string &bytes) : bytes_(bytes)
{
    Require(bytes.size() <= CalibrationCsv::BYTE_LIMIT);
    position_ = bytes.compare(0U, 3U, "\xef\xbb\xbf") == 0 ? 3U : 0U;
}

bool Rows::Next(std::vector<std::string> &row)
{
    row.clear();
    if (position_ == bytes_.size()) {
        return false;
    }
    for (;;) {
        Require(row.size() < 98U);
        row.push_back(Cell());
        if (position_ == bytes_.size()) {
            return true;
        }
        const char separator = bytes_[position_++];
        if (separator == '\n') {
            return true;
        }
        if (separator == '\r') {
            Require(position_ < bytes_.size() && bytes_[position_++] == '\n');
            return true;
        }
        Require(separator == ',');
    }
}

std::string Rows::Cell()
{
    if (position_ < bytes_.size() && bytes_[position_] == '"') {
        return QuotedCell();
    }
    return UnquotedCell();
}

std::string Rows::UnquotedCell()
{
    std::string cell;
    while (position_ < bytes_.size()) {
        const char value = bytes_[position_];
        if (value == ',' || value == '\r' || value == '\n') {
            return cell;
        }
        ++position_;
        Require(value != '"');
        Require(cell.size() < 1024U);
        cell += value;
    }
    return cell;
}

std::string Rows::QuotedCell()
{
    std::string cell;
    ++position_;
    while (position_ < bytes_.size()) {
        const char value = bytes_[position_++];
        if (value == '"') {
            if (position_ == bytes_.size() || bytes_[position_] != '"') {
                return cell;
            }
            ++position_;
        }
        Require(cell.size() < 1024U);
        cell += value;
    }
    throw std::invalid_argument("Invalid calibration CSV structure, metadata or numeric data");
}

double Numeric(const std::string &text)
{
    Require(!text.empty() && text.size() <= 64U);
    Require((text.front() == '-' || (text.front() >= '0' && text.front() <= '9')) && text.back() >= '0' &&
            text.back() <= '9');
    // 复用既有严格 JSON 数字语法、classic locale 与非正规数处理，单个数值恰占一个节点。
    try {
        const auto value = ParseJson(text, 1U);
        Require(value.type == Json::Type::NUMBER && std::isfinite(value.number));
        return value.number;
    } catch (const std::runtime_error &) {
        throw std::invalid_argument("Invalid calibration CSV number");
    }
}

std::uint64_t Identifier(const std::string &text)
{
    Require(!text.empty());
    std::uint64_t result = 0U;
    for (const char digit : text) {
        Require(digit >= '0' && digit <= '9');
        const auto value = static_cast<unsigned>(digit - '0');
        Require(result <= ((std::numeric_limits<std::uint64_t>::max)() - value) / 10U);
        result = result * 10U + value;
    }
    return result;
}
}  // 命名空间 CalibrationCsvDetail
}  // 命名空间 VnaWeb
