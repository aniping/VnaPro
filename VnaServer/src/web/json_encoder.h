#ifndef VNAPRO_WEB_JSON_ENCODER_H
#define VNAPRO_WEB_JSON_ENCODER_H

#include <memory>
#include <string>

#include "web/json.h"

namespace vna_web {
// The entire response is retained until complete, so Content-Length is exact.
// Traversal and numeric formatting are bounded per event-loop tick.
class JsonEncoder {
public:
    explicit JsonEncoder(Json value);
    ~JsonEncoder();
    bool advance(unsigned budget = 256U);
    std::string take();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::string stringifyJson(const Json &value);
}  // namespace vna_web

#endif
