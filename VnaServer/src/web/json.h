/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_JSON_H
#define VNAPRO_WEB_JSON_H

#include <map>
#include <string>
#include <vector>

namespace VnaWeb {
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
    // 构造 null 值；复制和移动均保留完整的值树。
    Json() noexcept = default;
    ~Json() = default;
    Json(const Json &) = default;
    Json &operator=(const Json &) = default;
    Json(Json &&) noexcept = default;
    Json &operator=(Json &&) noexcept = default;

    // 构造对应的 JSON 标量。
    explicit Json(bool value) noexcept : type(Type::BOOLEAN), boolean(value)
    {
    }

    // 构造对应的 JSON 标量。
    explicit Json(double value) noexcept : type(Type::NUMBER), number(value)
    {
    }

    // 构造对应的 JSON 标量。
    explicit Json(unsigned value) noexcept : Json(static_cast<double>(value))
    {
    }

    // 复制 UTF-8 字符串；编码时先验证内容，再写入 JSON。
    explicit Json(const std::string &value) : type(Type::STRING), string(value)
    {
    }

    // 复制以 NUL 结尾的 UTF-8 字符串；传入 nullptr 时抛出 invalid_argument。
    explicit Json(const char *value);

    // 构造空对象。
    static Json MakeObject() noexcept;
    // 构造空数组。
    static Json MakeArray() noexcept;
    // 返回或插入成员；当前值不是对象时抛出异常。
    Json &operator[](const std::string &key);
    // 借用已有成员；不存在时返回 nullptr，不插入新成员。
    const Json *Find(const std::string &key) const noexcept;
};

// 拒绝无效 UTF-8、过长编码、代理区码点及超出 Unicode 范围的值。
// 字符串值允许包含控制字符，编码时对其进行转义。
void ValidateJsonString(const std::string &text);
}  // 命名空间 VnaWeb

#endif
