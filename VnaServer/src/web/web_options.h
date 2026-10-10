#ifndef VNAPRO_VNASERVER_SRC_WEB_WEBOPTIONS_H
#define VNAPRO_VNASERVER_SRC_WEB_WEBOPTIONS_H
#include <cstdint>
#include <string>

namespace vna_web {

struct WebOptions {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8080;
    std::string staticRoot;
    bool enabled = true;
};

}  // namespace vna_web

#endif
