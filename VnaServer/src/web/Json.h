#ifndef VNAPRO_WEB_JSON_H
#define VNAPRO_WEB_JSON_H

#include <map>
#include <string>
#include <vector>

namespace vna_web {
struct Json {
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    Json() = default;

    explicit Json(bool value) : type(Type::Boolean), boolean(value)
    {
    }

    explicit Json(double value) : type(Type::Number), number(value)
    {
    }

    explicit Json(unsigned value) : Json(static_cast<double>(value))
    {
    }

    explicit Json(const std::string& value) : type(Type::String), string(value)
    {
    }

    explicit Json(const char* value) : Json(std::string(value))
    {
    }

    static Json makeObject();
    static Json makeArray();
    Json& operator[](const std::string& key);
    const Json* find(const std::string& key) const;
};
}  // namespace vna_web

#endif
