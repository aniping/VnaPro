#include "web/Json.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vna_web {
Json Json::makeObject()
{
    Json value;
    value.type = Type::Object;
    return value;
}
Json Json::makeArray()
{
    Json value;
    value.type = Type::Array;
    return value;
}
Json& Json::operator[](const std::string& key)
{
    if (type != Type::Object)
        throw std::runtime_error("JSON value is not an object");
    return object[key];
}
const Json* Json::find(const std::string& key) const
{
    if (type != Type::Object)
        return nullptr;
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &found->second;
}
namespace {
std::string escaped(const std::string& text)
{
    std::string result = "\"";
    const char hex[] = "0123456789abcdef";
    for (unsigned char character : text) {
        if (character == '"' || character == '\\') {
            result += '\\';
            result += static_cast<char>(character);
        } else if (character < 32U) {
            result += "\\u00";
            result += hex[character >> 4U];
            result += hex[character & 15U];
        } else
            result += static_cast<char>(character);
    }
    return result + '"';
}
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
class Parser {
public:
    explicit Parser(const std::string& value) : text(value) {}
    const std::string& text;
    std::size_t position = 0;
    unsigned nodes = 0;
    void space()
    {
        while (position < text.size() &&
               (text[position] == ' ' || text[position] == '\r' || text[position] == '\n' || text[position] == '\t'))
            ++position;
    }
    bool take(char character)
    {
        space();
        if (position < text.size() && text[position] == character) {
            ++position;
            return true;
        }
        return false;
    }
    void require(char character)
    {
        if (!take(character))
            throw std::runtime_error("Invalid JSON delimiter");
    }
    unsigned hex4()
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
    std::string quoted()
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
    Json value(unsigned depth = 0)
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
};
}  // namespace
Json parseJson(const std::string& text)
{
    Parser parser(text);
    Json result = parser.value();
    parser.space();
    if (parser.position != text.size())
        throw std::runtime_error("Trailing JSON data");
    return result;
}
class JsonEncoder::Impl {
public:
    struct Frame {
        const Json* value;
        std::size_t index = 0;
        bool opened = false;
        std::map<std::string, Json>::const_iterator member;
        explicit Frame(const Json* item) : value(item), member(item->object.begin()) {}
    };
    explicit Impl(Json root) : value(std::move(root)) { stack.emplace_back(&value); }
    Json value;
    std::vector<Frame> stack;
    std::string output;
    bool advance(unsigned budget)
    {
        for (unsigned count = 0; count < budget && !stack.empty(); ++count) {
            Frame& frame = stack.back();
            const Json& json = *frame.value;
            if (json.type == Json::Type::Array || json.type == Json::Type::Object) {
                const bool array = json.type == Json::Type::Array;
                if (!frame.opened) {
                    output += array ? '[' : '{';
                    frame.opened = true;
                } else if ((array && frame.index == json.array.size()) ||
                           (!array && frame.member == json.object.end())) {
                    output += array ? ']' : '}';
                    stack.pop_back();
                } else {
                    if (frame.index != 0U)
                        output += ',';
                    const Json* child;
                    if (array)
                        child = &json.array[frame.index];
                    else {
                        output += escaped(frame.member->first) + ':';
                        child = &frame.member++->second;
                    }
                    ++frame.index;
                    stack.emplace_back(child);
                }
            } else {
                if (json.type == Json::Type::Null)
                    output += "null";
                else if (json.type == Json::Type::Boolean)
                    output += json.boolean ? "true" : "false";
                else if (json.type == Json::Type::String)
                    output += escaped(json.string);
                else if (!std::isfinite(json.number))
                    output += "null";
                else {
                    std::ostringstream number;
                    number.imbue(std::locale::classic());
                    number << std::setprecision(17) << json.number;
                    output += number.str();
                }
                stack.pop_back();
            }
            if (output.size() > 4U * 1024U * 1024U)
                throw std::runtime_error("JSON output exceeds 4 MiB");
        }
        return stack.empty();
    }
};
JsonEncoder::JsonEncoder(Json value) : impl_(new Impl(std::move(value))) {}
JsonEncoder::~JsonEncoder() = default;
bool JsonEncoder::advance(unsigned budget)
{
    return impl_->advance(budget);
}
std::string JsonEncoder::take()
{
    return std::move(impl_->output);
}
std::string stringifyJson(const Json& value)
{
    JsonEncoder encoder(value);
    while (!encoder.advance()) {
    }
    return encoder.take();
}
}  // namespace vna_web
