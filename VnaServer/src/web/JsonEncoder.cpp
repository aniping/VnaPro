#include "web/JsonEncoder.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vna_web {
namespace {
std::string escaped(const std::string &text)
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
        } else {
            result += static_cast<char>(character);
        }
    }
    return result + '"';
}
}  // namespace

class JsonEncoder::Impl {
public:
    struct Frame {
        const Json *value;
        std::size_t index = 0;
        bool opened = false;
        std::map<std::string, Json>::const_iterator member;

        explicit Frame(const Json *item) : value(item), member(item->object.begin())
        {
        }
    };

    explicit Impl(Json root) : value(std::move(root))
    {
        stack.emplace_back(&value);
    }

    Json value;
    std::vector<Frame> stack;
    std::string output;

    bool advance(unsigned budget)
    {
        for (unsigned count = 0; count < budget && !stack.empty(); ++count) {
            Frame &frame = stack.back();
            const Json &json = *frame.value;
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
                    if (frame.index != 0U) {
                        output += ',';
                    }
                    const Json *child;
                    if (array) {
                        child = &json.array[frame.index];
                    } else {
                        output += escaped(frame.member->first) + ':';
                        child = &frame.member++->second;
                    }
                    ++frame.index;
                    stack.emplace_back(child);
                }
            } else {
                if (json.type == Json::Type::Null) {
                    output += "null";
                } else if (json.type == Json::Type::Boolean) {
                    output += json.boolean ? "true" : "false";
                } else if (json.type == Json::Type::String) {
                    output += escaped(json.string);
                } else if (!std::isfinite(json.number)) {
                    output += "null";
                } else {
                    std::ostringstream number;
                    number.imbue(std::locale::classic());
                    number << std::setprecision(17) << json.number;
                    output += number.str();
                }
                stack.pop_back();
            }
            if (output.size() > 4U * 1024U * 1024U) {
                throw std::runtime_error("JSON output exceeds 4 MiB");
            }
        }
        return stack.empty();
    }
};

JsonEncoder::JsonEncoder(Json value) : impl_(new Impl(std::move(value)))
{
}

JsonEncoder::~JsonEncoder() = default;

bool JsonEncoder::advance(unsigned budget)
{
    return impl_->advance(budget);
}

std::string JsonEncoder::take()
{
    return std::move(impl_->output);
}

std::string stringifyJson(const Json &value)
{
    JsonEncoder encoder(value);
    while (!encoder.advance()) {
    }
    return encoder.take();
}
}  // namespace vna_web
