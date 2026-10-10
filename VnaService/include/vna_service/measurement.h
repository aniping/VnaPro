#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#include <string>

namespace vna_service {

struct Measurement {
    std::string name;
    char receiverKind = 'a';
    unsigned receiverPort = 1U;
    unsigned sourcePort = 1U;
    std::string parameter() const;
};

}  // namespace vna_service

#endif
