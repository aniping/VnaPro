/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/json_parser.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <locale>
#include <stdexcept>

#ifdef _WIN32
#include <locale.h>
#endif

namespace VnaWeb {
namespace {
#ifdef _WIN32
struct NumberLocale final {
    _locale_t value = _create_locale(LC_NUMERIC, "C");

    ~NumberLocale()
    {
        if (value != nullptr) {
            _free_locale(value);
        }
    }
};
#endif

bool TryConvertNumber(const std::string &token, double &value)
{
#ifdef _WIN32
    // 独立且只读的 C locale 不改变其他线程的地区设置。
    static const NumberLocale locale;
    if (locale.value == nullptr) {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    value = _strtod_l(token.c_str(), &end, locale.value);
    const int conversionError = errno;
    // 部分 CRT 对非正规数未报告 ERANGE，舍入也可能不同；交回原经典流保证兼容。
    return conversionError == 0 && end == token.c_str() + token.size() && std::isfinite(value) &&
           (value == 0.0 || std::abs(value) >= std::numeric_limits<double>::min());
#else
    (void)token;
    (void)value;
    return false;
#endif
}

constexpr unsigned UTF8_ASCII_LIMIT = 0x80U;
constexpr unsigned UTF8_TWO_BYTE_LIMIT = 0x800U;
constexpr unsigned UTF8_THREE_BYTE_LIMIT = 0x10000U;
constexpr unsigned UTF8_CONTINUATION_PREFIX = 0x80U;
constexpr unsigned UTF8_TWO_BYTE_PREFIX = 0xc0U;
constexpr unsigned UTF8_THREE_BYTE_PREFIX = 0xe0U;
constexpr unsigned UTF8_FOUR_BYTE_PREFIX = 0xf0U;
constexpr unsigned UTF8_PAYLOAD_BITS = 6U;
constexpr unsigned UTF8_PAYLOAD_MASK = 63U;
constexpr unsigned HEX_ESCAPE_DIGITS = 4U;
constexpr unsigned HEX_RADIX = 16U;
constexpr unsigned HEX_LETTER_OFFSET = 10U;
constexpr unsigned CONTROL_CHARACTER_LIMIT = 32U;
constexpr std::size_t UNICODE_ESCAPE_PREFIX_LENGTH = 2U;

void AppendUtf8(std::string &output, unsigned scalar)
{
    if (scalar < UTF8_ASCII_LIMIT) {
        output += static_cast<char>(scalar);
    } else if (scalar < UTF8_TWO_BYTE_LIMIT) {
        output += static_cast<char>(UTF8_TWO_BYTE_PREFIX | (scalar >> UTF8_PAYLOAD_BITS));
        output += static_cast<char>(UTF8_CONTINUATION_PREFIX | (scalar & UTF8_PAYLOAD_MASK));
    } else if (scalar < UTF8_THREE_BYTE_LIMIT) {
        output += static_cast<char>(UTF8_THREE_BYTE_PREFIX | (scalar >> (2U * UTF8_PAYLOAD_BITS)));
        output += static_cast<char>(UTF8_CONTINUATION_PREFIX | ((scalar >> UTF8_PAYLOAD_BITS) & UTF8_PAYLOAD_MASK));
        output += static_cast<char>(UTF8_CONTINUATION_PREFIX | (scalar & UTF8_PAYLOAD_MASK));
    } else {
        output += static_cast<char>(UTF8_FOUR_BYTE_PREFIX | (scalar >> (3U * UTF8_PAYLOAD_BITS)));
        output +=
            static_cast<char>(UTF8_CONTINUATION_PREFIX | ((scalar >> (2U * UTF8_PAYLOAD_BITS)) & UTF8_PAYLOAD_MASK));
        output += static_cast<char>(UTF8_CONTINUATION_PREFIX | ((scalar >> UTF8_PAYLOAD_BITS) & UTF8_PAYLOAD_MASK));
        output += static_cast<char>(UTF8_CONTINUATION_PREFIX | (scalar & UTF8_PAYLOAD_MASK));
    }
}
}  // 匿名命名空间

JsonParser::JsonParser(const std::string &input, unsigned maxNodes) : text_(input), maxNodes_(maxNodes)
{
    numberConverter_.imbue(std::locale::classic());
}

Json JsonParser::Parse()
{
    position_ = 0U;
    nodes_ = 0U;
    Json result = Value(0U);
    Space();
    if (position_ != text_.size()) {
        throw std::runtime_error("Trailing JSON data");
    }
    return result;
}

void JsonParser::Space()
{
    while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\r' ||
                                           text_[position_] == '\n' || text_[position_] == '\t')) {
        ++position_;
    }
}

bool JsonParser::Take(char character)
{
    Space();
    if (position_ < text_.size() && text_[position_] == character) {
        ++position_;
        return true;
    }
    return false;
}

void JsonParser::Require(char character)
{
    if (!Take(character)) {
        throw std::runtime_error("Invalid JSON delimiter");
    }
}

unsigned JsonParser::Hex4()
{
    unsigned scalar = 0U;
    for (unsigned index = 0U; index < HEX_ESCAPE_DIGITS; ++index) {
        if (position_ == text_.size()) {
            throw std::runtime_error("Incomplete Unicode escape");
        }
        const char digit = text_[position_++];
        unsigned nibble = 0U;
        if (digit >= '0' && digit <= '9') {
            nibble = digit - '0';
        } else if (digit >= 'a' && digit <= 'f') {
            nibble = digit - 'a' + HEX_LETTER_OFFSET;
        } else if (digit >= 'A' && digit <= 'F') {
            nibble = digit - 'A' + HEX_LETTER_OFFSET;
        } else {
            throw std::runtime_error("Invalid Unicode escape");
        }
        scalar = scalar * HEX_RADIX + nibble;
    }
    return scalar;
}

std::string JsonParser::Quoted()
{
    Require('"');
    std::string output;
    while (position_ < text_.size()) {
        const unsigned char character = static_cast<unsigned char>(text_[position_++]);
        if (character == '"') {
            ValidateJsonString(output);
            return output;
        }
        if (character < CONTROL_CHARACTER_LIMIT) {
            throw std::runtime_error("Control character in JSON string");
        }
        if (character != '\\') {
            output += static_cast<char>(character);
            continue;
        }
        Escape(output);
    }
    throw std::runtime_error("Unterminated JSON string");
}

unsigned JsonParser::UnicodeScalar()
{
    constexpr unsigned highBegin = 0xd800U;
    constexpr unsigned highEnd = 0xdbffU;
    constexpr unsigned lowBegin = 0xdc00U;
    constexpr unsigned lowEnd = 0xdfffU;
    unsigned scalar = Hex4();
    if (scalar >= highBegin && scalar <= highEnd) {
        if (text_.size() - position_ < UNICODE_ESCAPE_PREFIX_LENGTH || text_[position_] != '\\' ||
            text_[position_ + 1U] != 'u') {
            throw std::runtime_error("Missing low surrogate");
        }
        position_ += UNICODE_ESCAPE_PREFIX_LENGTH;
        const unsigned low = Hex4();
        if (low < lowBegin || low > lowEnd) {
            throw std::runtime_error("Invalid low surrogate");
        }
        constexpr unsigned supplementaryBegin = 0x10000U;
        constexpr unsigned surrogateBits = 10U;
        scalar = supplementaryBegin + ((scalar - highBegin) << surrogateBits) + (low - lowBegin);
    } else if (scalar >= lowBegin && scalar <= lowEnd) {
        throw std::runtime_error("Unpaired low surrogate");
    }
    return scalar;
}

void JsonParser::Escape(std::string &output)
{
    if (position_ == text_.size()) {
        throw std::runtime_error("Incomplete JSON escape");
    }
    const char code = text_[position_++];
    switch (code) {
        case '"':
        case '\\':
        case '/':
            output += code;
            break;
        case 'b':
            output += '\b';
            break;
        case 'f':
            output += '\f';
            break;
        case 'n':
            output += '\n';
            break;
        case 'r':
            output += '\r';
            break;
        case 't':
            output += '\t';
            break;
        case 'u':
            AppendUtf8(output, UnicodeScalar());
            break;
        default:
            throw std::runtime_error("Invalid JSON escape");
    }
}

Json JsonParser::Value(unsigned depth)
{
    constexpr unsigned maxDepth = 16U;
    Space();
    ++nodes_;
    if (depth > maxDepth || nodes_ > maxNodes_ || position_ == text_.size()) {
        throw std::runtime_error("JSON structure limit or missing value");
    }
    switch (text_[position_]) {
        case '"':
            return Json(Quoted());
        case '{':
            return Object(depth);
        case '[':
            return Array(depth);
        case 'n':
        case 't':
        case 'f':
            break;
        default:
            return Number();
    }
    for (const auto &keyword : {std::string("null"), std::string("true"), std::string("false")}) {
        if (text_.compare(position_, keyword.size(), keyword) == 0) {
            position_ += keyword.size();
            return keyword == "null" ? Json() : Json(keyword == "true");
        }
    }
    return Number();
}

Json JsonParser::Object(unsigned depth)
{
    ++position_;
    Json result = Json::MakeObject();
    if (Take('}')) {
        return result;
    }
    do {
        const std::string key = Quoted();
        Require(':');
        if (result.object.count(key) != 0U) {
            throw std::runtime_error("Duplicate JSON member");
        }
        result.object.emplace(key, Value(depth + 1U));
    } while (Take(','));
    Require('}');
    return result;
}

Json JsonParser::Array(unsigned depth)
{
    ++position_;
    Json result = Json::MakeArray();
    if (Take(']')) {
        return result;
    }
    do {
        result.array.push_back(Value(depth + 1U));
    } while (Take(','));
    Require(']');
    return result;
}

void JsonParser::Digits()
{
    const auto begin = position_;
    while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
        ++position_;
    }
    if (begin == position_) {
        throw std::runtime_error("Missing numeric digits");
    }
}

void JsonParser::IntegerPart()
{
    if (text_[position_] == '-') {
        ++position_;
    }
    if (position_ == text_.size()) {
        throw std::runtime_error("Invalid JSON number");
    }
    if (text_[position_] == '0') {
        ++position_;
        return;
    }
    if (text_[position_] < '1' || text_[position_] > '9') {
        throw std::runtime_error("Invalid JSON number");
    }
    Digits();
}

Json JsonParser::Number()
{
    const std::size_t begin = position_;
    IntegerPart();
    if (position_ < text_.size() && text_[position_] == '.') {
        ++position_;
        Digits();
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
        ++position_;
        if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
            ++position_;
        }
        Digits();
    }
    const std::string token = text_.substr(begin, position_ - begin);
    double parsed = 0.0;
    if (!TryConvertNumber(token, parsed)) {
        numberConverter_.clear();
        numberConverter_.str(token);
        numberConverter_ >> parsed;
        if (numberConverter_.fail() || !numberConverter_.eof() || !std::isfinite(parsed)) {
            throw std::runtime_error("JSON number is not finite");
        }
    }
    return Json(parsed);
}

Json ParseJson(const std::string &input, unsigned maxNodes)
{
    JsonParser parser(input, maxNodes);
    return parser.Parse();
}
}  // 命名空间 VnaWeb
