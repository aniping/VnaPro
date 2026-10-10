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
    // Borrow one input document, which must outlive this parser and every Parse call.
    explicit JsonParser(const std::string &input) noexcept;
    JsonParser(std::string &&) = delete;
    JsonParser(const std::string &&) = delete;
    ~JsonParser() = default;
    JsonParser(const JsonParser &) = delete;
    JsonParser &operator=(const JsonParser &) = delete;
    JsonParser(JsonParser &&) = delete;
    JsonParser &operator=(JsonParser &&) = delete;
    // Restart at the beginning and parse one bounded UTF-8 document; malformed input throws runtime_error.
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
};

// Parse one complete JSON document with bounded nesting and node count.
Json ParseJson(const std::string &input);
}  // namespace VnaWeb

#endif
