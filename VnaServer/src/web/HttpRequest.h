#ifndef VNAPRO_VNASERVER_SRC_WEB_HTTPREQUEST_H
#define VNAPRO_VNASERVER_SRC_WEB_HTTPREQUEST_H
#include <map>
#include <string>

namespace vna_web {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;  // Lowercase names.
    std::string body;
};

}  // namespace vna_web

#endif
