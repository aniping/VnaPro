#ifndef VNAPRO_VNASERVER_SRC_WEB_HTTPRESPONSE_H
#define VNAPRO_VNASERVER_SRC_WEB_HTTPRESPONSE_H
#include "web/json.h"

namespace vna_web {

struct HttpResponse {
    unsigned status = 200;
    Json json;
};

}  // namespace vna_web

#endif
