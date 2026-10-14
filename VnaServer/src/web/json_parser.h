/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_JSON_PARSER_H
#define VNAPRO_WEB_JSON_PARSER_H

#include <cstddef>
#include <string>

#include "web/json.h"

namespace VnaWeb {
class JsonParser final {
public:
    // 借用输入文档，其生命周期必须覆盖本解析器及每次 Parse 调用。
    explicit JsonParser(const std::string &input, unsigned maxNodes = 4096U) noexcept;
    JsonParser(std::string &&) = delete;
    JsonParser(const std::string &&) = delete;
    ~JsonParser() = default;
    JsonParser(const JsonParser &) = delete;
    JsonParser &operator=(const JsonParser &) = delete;
    JsonParser(JsonParser &&) = delete;
    JsonParser &operator=(JsonParser &&) = delete;
    // 从头解析一个有界的 UTF-8 文档；输入格式错误时抛出 runtime_error。
    Json Parse();

private:
    void Space();
    bool Take(char character);
    void Require(char character);
    unsigned Hex4();
    unsigned UnicodeScalar();
    void Escape(std::string &output);
    std::string Quoted();
    Json Value(unsigned depth);
    Json Object(unsigned depth);
    Json Array(unsigned depth);
    Json Number();
    void IntegerPart();
    void Digits();
    const std::string &text_;
    std::size_t position_ = 0U;
    unsigned nodes_ = 0U;
    unsigned maxNodes_;
};

// 解析一个完整的 JSON 文档，并限制嵌套深度和节点数量。
Json ParseJson(const std::string &input, unsigned maxNodes = 4096U);
}  // 命名空间 VnaWeb

#endif
