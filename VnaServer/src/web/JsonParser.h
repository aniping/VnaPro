#ifndef VNAPRO_WEB_JSON_PARSER_H
#define VNAPRO_WEB_JSON_PARSER_H

#include <cstddef>
#include <string>

#include "web/Json.h"

namespace vna_web {
// Consumes one input document; the input must remain alive until parse() returns.
class JsonParser {
public:
    explicit JsonParser(const std::string& input);
    JsonParser(std::string&&) = delete;
    JsonParser(const std::string&&) = delete;
    Json parse();

private:
    void space();
    bool take(char character);
    void require(char character);
    unsigned hex4();
    std::string quoted();
    Json value(unsigned depth = 0);

    const std::string& text;
    std::size_t position = 0;
    unsigned nodes = 0;
};

Json parseJson(const std::string& text);
}  // namespace vna_web

#endif
