/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_LISTEN_OPTIONS_H
#define VNAPRO_SERVER_LISTEN_OPTIONS_H

#include <cstdint>
#include <functional>
#include <string>

#include "web/http_types.h"

namespace VnaServer {
struct ListenOptions final {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 5025;
    VnaWeb::WebOptions web;
    std::function<void(std::uint16_t)> onListening;
};
}  // namespace VnaServer

#endif
