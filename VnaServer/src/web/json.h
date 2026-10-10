/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_JSON_H
#define VNAPRO_WEB_JSON_H

#include <map>
#include <string>
#include <vector>

namespace vna_web {
struct Json final {
    enum class Type {
        NULL_VALUE,
        BOOLEAN,
        NUMBER,
        STRING,
        ARRAY,
        OBJECT
    };
    Type type = Type::NULL_VALUE;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    // Construct null; copies and moves preserve the complete value tree.
    Json() noexcept = default;
    ~Json() = default;
    Json(const Json &) = default;
    Json &operator=(const Json &) = default;
    Json(Json &&) noexcept = default;
    Json &operator=(Json &&) noexcept = default;

    // Construct the corresponding JSON scalar.
    explicit Json(bool value) noexcept : type(Type::BOOLEAN), boolean(value)
    {
    }

    // Construct the corresponding JSON scalar.
    explicit Json(double value) noexcept : type(Type::NUMBER), number(value)
    {
    }

    // Construct the corresponding JSON scalar.
    explicit Json(unsigned value) noexcept : Json(static_cast<double>(value))
    {
    }

    // Copy a UTF-8 string; encoding validates its contents before writing JSON.
    explicit Json(const std::string &value) : type(Type::STRING), string(value)
    {
    }

    // Copy a NUL-terminated UTF-8 string; nullptr throws invalid_argument.
    explicit Json(const char *value);

    // Construct an empty object.
    static Json MakeObject() noexcept;
    // Construct an empty array.
    static Json MakeArray() noexcept;
    // Return or insert a member; throw unless this value is an object.
    Json &operator[](const std::string &key);
    // Borrow an existing member, or return nullptr without inserting.
    const Json *Find(const std::string &key) const noexcept;
};

// Reject malformed UTF-8, overlong encodings, surrogate scalars, and values beyond Unicode.
// Embedded control characters remain allowed as string values and are escaped during encoding.
void ValidateJsonString(const std::string &text);
}  // namespace vna_web

#endif
