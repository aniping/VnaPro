/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_HTTP_TYPES_H
#define VNAPRO_WEB_HTTP_TYPES_H
#include <cstdint>
#include <map>
#include <string>

#include "web/json.h"

namespace VnaWeb {
struct HttpRequest final {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;  // Lowercase names.
    std::string body;
};

struct HttpResponse final {
    unsigned status = 200;
    Json json;
};

struct WebOptions final {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8080;
    std::string staticRoot;
    bool enabled = true;
};
}  // namespace VnaWeb
#endif
