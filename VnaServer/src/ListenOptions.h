#ifndef VNAPRO_SERVER_LISTEN_OPTIONS_H
#define VNAPRO_SERVER_LISTEN_OPTIONS_H

#include <cstdint>
#include <functional>
#include <string>

#include "web/WebOptions.h"

namespace vna_server {
struct ListenOptions {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 5025;
    vna_web::WebOptions web;
    std::function<void(std::uint16_t)> onListening;
};
}  // namespace vna_server

#endif
