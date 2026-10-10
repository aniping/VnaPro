#include "web/JsonParser.h"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace vna_web {
namespace {
void utf8(std::string& output, unsigned value)
{
    if (value < 0x80U)
        output += static_cast<char>(value);
    else if (value < 0x800U) {
        output += static_cast<char>(0xc0U | (value >> 6U));
        output += static_cast<char>(0x80U | (value & 63U));
    } else if (value < 0x10000U) {
        output += static_cast<char>(0xe0U | (value >> 12U));
        output += static_cast<char>(0x80U | ((value >> 6U) & 63U));
        output += static_cast<char>(0x80U | (value & 63U));
    } else {
        output += static_cast<char>(0xf0U | (value >> 18U));
        output += static_cast<char>(0x80U | ((value >> 12U) & 63U));
        output += static_cast<char>(0x80U | ((value >> 6U) & 63U));
        output += static_cast<char>(0x80U | (value & 63U));
    }
}
}  // namespace

JsonParser::JsonParser(const std::string& input) : text(input)
{
}

Json JsonParser::parse()
{
    Json result = value();
    space();
    if (position != text.size())
        throw std::runtime_error("Trailing JSON data");
    return result;
}

void JsonParser::space()
{
    while (position < text.size() &&
           (text[position] == ' ' || text[position] == '\r' || text[position] == '\n' || text[position] == '\t'))
        ++position;
}

bool JsonParser::take(char character)
{
    space();
    if (position < text.size() && text[position] == character) {
        ++position;
        return true;
    }
    return false;
}

void JsonParser::require(char character)
{
    if (!take(character))
        throw std::runtime_error("Invalid JSON delimiter");
}

unsigned JsonParser::hex4()
{
    unsigned value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        if (position == text.size())
            throw std::runtime_error("Incomplete Unicode escape");
        const char digit = text[position++];
        unsigned nibble;
        if (digit >= '0' && digit <= '9')
            nibble = digit - '0';
        else if (digit >= 'a' && digit <= 'f')
            nibble = digit - 'a' + 10U;
        else if (digit >= 'A' && digit <= 'F')
            nibble = digit - 'A' + 10U;
        else
            throw std::runtime_error("Invalid Unicode escape");
        value = value * 16U + nibble;
    }
    return value;
}

std::string JsonParser::quoted()
{
    require('"');
    std::string value;
    while (position < text.size()) {
        const unsigned char character = static_cast<unsigned char>(text[position++]);
        if (character == '"')
            return value;
        if (character < 32U)
            throw std::runtime_error("Control character in JSON string");
        if (character != '\\') {
            value += static_cast<char>(character);
            continue;
        }
        if (position == text.size())
            throw std::runtime_error("Incomplete JSON escape");
        const char code = text[position++];
        if (code == '"' || code == '\\' || code == '/')
            value += code;
        else if (code == 'b')
            value += '\b';
        else if (code == 'f')
            value += '\f';
        else if (code == 'n')
            value += '\n';
        else if (code == 'r')
            value += '\r';
        else if (code == 't')
            value += '\t';
        else if (code == 'u') {
            unsigned scalar = hex4();
            if (scalar >= 0xd800U && scalar <= 0xdbffU) {
                if (position + 2U > text.size() || text[position++] != '\\' || text[position++] != 'u')
                    throw std::runtime_error("Missing low surrogate");
                const unsigned low = hex4();
                if (low < 0xdc00U || low > 0xdfffU)
                    throw std::runtime_error("Invalid low surrogate");
                scalar = 0x10000U + ((scalar - 0xd800U) << 10U) + low - 0xdc00U;
            } else if (scalar >= 0xdc00U && scalar <= 0xdfffU)
                throw std::runtime_error("Unpaired low surrogate");
            utf8(value, scalar);
        } else
            throw std::runtime_error("Invalid JSON escape");
    }
    throw std::runtime_error("Unterminated JSON string");
}

Json JsonParser::value(unsigned depth)
{
    space();
    if (depth > 16U || ++nodes > 4096U || position == text.size())
        throw std::runtime_error("JSON structure limit or missing value");
    const char first = text[position];
    if (first == '"')
        return Json(quoted());
    if (first == '{') {
        ++position;
        Json result = Json::makeObject();
        if (take('}'))
            return result;
        do {
            const std::string key = quoted();
            require(':');
            if (result.object.count(key) != 0U)
                throw std::runtime_error("Duplicate JSON member");
            result.object.emplace(key, value(depth + 1U));
        } while (take(','));
        require('}');
        return result;
    }
    if (first == '[') {
        ++position;
        Json result = Json::makeArray();
        if (take(']'))
            return result;
        do {
            result.array.push_back(value(depth + 1U));
        } while (take(','));
        require(']');
        return result;
    }
    for (const auto& keyword : {std::string("null"), std::string("true"), std::string("false")}) {
        if (text.compare(position, keyword.size(), keyword) == 0) {
            position += keyword.size();
            if (keyword == "null")
                return Json();
            return Json(keyword == "true");
        }
    }
    const std::size_t begin = position;
    if (text[position] == '-')
        ++position;
    if (position == text.size())
        throw std::runtime_error("Invalid JSON number");
    if (text[position] == '0')
        ++position;
    else {
        if (text[position] < '1' || text[position] > '9')
            throw std::runtime_error("Invalid JSON number");
        while (position < text.size() && text[position] >= '0' && text[position] <= '9')
            ++position;
    }
    if (position < text.size() && text[position] == '.') {
        const auto digits = ++position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9')
            ++position;
        if (digits == position)
            throw std::runtime_error("Missing decimal digits");
    }
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
        ++position;
        if (position < text.size() && (text[position] == '+' || text[position] == '-'))
            ++position;
        const auto digits = position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9')
            ++position;
        if (digits == position)
            throw std::runtime_error("Missing exponent digits");
    }
    const std::string number = text.substr(begin, position - begin);
    char* end = nullptr;
    const double parsed = std::strtod(number.c_str(), &end);
    if (end != number.c_str() + number.size() || !std::isfinite(parsed))
        throw std::runtime_error("JSON number is not finite");
    return Json(parsed);
}

Json parseJson(const std::string& text)
{
    JsonParser parser(text);
    return parser.parse();
}
}  // namespace vna_web
