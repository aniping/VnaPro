#include "web/Json.h"

#include <stdexcept>

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

Json &Json::operator[](const std::string &key)
{
    if (type != Type::Object) {
        throw std::runtime_error("JSON value is not an object");
    }
    return object[key];
}

const Json *Json::find(const std::string &key) const
{
    if (type != Type::Object) {
        return nullptr;
    }
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &found->second;
}
}  // namespace vna_web
