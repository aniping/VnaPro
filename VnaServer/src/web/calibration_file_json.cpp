/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#include "web/calibration_file_json.h"

#include <array>
#include <stdexcept>

#include "web/calibration_binary.h"
#include "web/calibration_csv.h"

namespace VnaWeb {
namespace {
constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const std::string &Text(const Json &body, const char *name)
{
    const auto *value = body.Find(name);
    if (value == nullptr || value->type != Json::Type::STRING) {
        throw std::invalid_argument(std::string("Invalid calibration file field: ") + name);
    }
    return value->string;
}

std::string EncodeBase64(const std::string &bytes)
{
    std::string result;
    result.reserve((bytes.size() + 2U) / 3U * 4U);
    for (std::size_t first = 0U; first < bytes.size(); first += 3U) {
        const auto left = bytes.size() - first;
        const unsigned bits = static_cast<unsigned char>(bytes[first]) << 16U |
                              (left > 1U ? static_cast<unsigned char>(bytes[first + 1U]) << 8U : 0U) |
                              (left > 2U ? static_cast<unsigned char>(bytes[first + 2U]) : 0U);
        result += ALPHABET[(bits >> 18U) & 63U];
        result += ALPHABET[(bits >> 12U) & 63U];
        result += left > 1U ? ALPHABET[(bits >> 6U) & 63U] : '=';
        result += left > 2U ? ALPHABET[bits & 63U] : '=';
    }
    return result;
}

const std::array<int, 256U> &Base64Digits()
{
    static const auto digits = [] {
        std::array<int, 256U> result{};
        result.fill(-1);
        for (unsigned index = 0U; index < 64U; ++index) {
            result[static_cast<unsigned char>(ALPHABET[index])] = static_cast<int>(index);
        }
        return result;
    }();
    return digits;
}

struct Base64Quartet final {
    std::array<int, 4U> digits;
    bool two;
    bool one;
};

void ValidatePadding(const Base64Quartet &quartet, bool last)
{
    if (quartet.two && !quartet.one) {
        throw std::invalid_argument("Calibration binary Base64 is not canonical");
    }
    if ((quartet.two || quartet.one) && !last) {
        throw std::invalid_argument("Calibration binary Base64 is not canonical");
    }
}

void ValidateUnusedBits(const Base64Quartet &quartet)
{
    if (quartet.two && (quartet.digits[1U] & 15) != 0) {
        throw std::invalid_argument("Calibration binary Base64 is not canonical");
    }
    if (quartet.one && !quartet.two && (quartet.digits[2U] & 3) != 0) {
        throw std::invalid_argument("Calibration binary Base64 is not canonical");
    }
}

Base64Quartet ReadQuartet(const std::string &text, std::size_t first)
{
    const auto &digits = Base64Digits();
    const bool two = text[first + 2U] == '=';
    const bool one = text[first + 3U] == '=';
    Base64Quartet quartet{
        {{digits[static_cast<unsigned char>(text[first])], digits[static_cast<unsigned char>(text[first + 1U])],
            two ? 0 : digits[static_cast<unsigned char>(text[first + 2U])],
            one ? 0 : digits[static_cast<unsigned char>(text[first + 3U])]}},
        two, one};
    for (const int digit : quartet.digits) {
        if (digit < 0) {
            throw std::invalid_argument("Calibration binary Base64 is not canonical");
        }
    }
    ValidatePadding(quartet, first + 4U == text.size());
    ValidateUnusedBits(quartet);
    return quartet;
}

void AppendQuartet(const Base64Quartet &quartet, std::string &result)
{
    const auto &digits = quartet.digits;
    const auto bits = static_cast<unsigned>((digits[0U] << 18) | (digits[1U] << 12) | (digits[2U] << 6) | digits[3U]);
    result += static_cast<char>((bits >> 16U) & 255U);
    if (!quartet.two) {
        result += static_cast<char>((bits >> 8U) & 255U);
    }
    if (!quartet.one) {
        result += static_cast<char>(bits & 255U);
    }
}

std::string DecodeBase64(const std::string &text)
{
    if (text.empty() || text.size() % 4U != 0U || text.size() > (CalibrationBinary::BYTE_LIMIT + 2U) / 3U * 4U) {
        throw std::invalid_argument("Calibration binary Base64 length is invalid");
    }
    std::string result;
    result.reserve(text.size() / 4U * 3U);
    for (std::size_t first = 0U; first < text.size(); first += 4U) {
        AppendQuartet(ReadQuartet(text, first), result);
    }
    if (result.size() > CalibrationBinary::BYTE_LIMIT) {
        throw std::invalid_argument("Calibration binary exceeds its byte limit");
    }
    return result;
}
}  // 匿名命名空间

Json CalibrationFileJson::Export(const VnaService::CalibrationSet &set, const std::string &format)
{
    if (format != "binary" && format != "csv") {
        throw std::invalid_argument("Calibration file format must be binary or csv");
    }
    const bool binary = format == "binary";
    const auto bytes = binary ? CalibrationBinary::Encode(set) : CalibrationCsv::Encode(set);
    Json result = Json::MakeObject();
    result["format"] = Json(format);
    result["encoding"] = Json(binary ? "base64" : "utf8");
    result["filename"] = Json("vnapro-" + std::to_string(set.id) + (binary ? ".vcal" : ".csv"));
    result["byteLength"] = Json(static_cast<unsigned>(bytes.size()));
    result["content"] = Json(binary ? EncodeBase64(bytes) : bytes);
    return result;
}

VnaService::CalibrationSetPtr CalibrationFileJson::Import(const Json &body)
{
    const auto &format = Text(body, "format");
    const auto &encoding = Text(body, "encoding");
    const auto &content = Text(body, "content");
    if (format == "binary" && encoding == "base64") {
        return CalibrationBinary::Decode(DecodeBase64(content));
    }
    if (format == "csv" && encoding == "utf8") {
        if (content.size() > CalibrationCsv::BYTE_LIMIT) {
            throw std::invalid_argument("Calibration CSV exceeds its byte limit");
        }
        return CalibrationCsv::Decode(content);
    }
    throw std::invalid_argument("Calibration file format and encoding do not match");
}
}  // 命名空间 VnaWeb
