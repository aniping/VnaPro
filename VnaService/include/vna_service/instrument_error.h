#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTERROR_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTERROR_H
#include <string>

namespace vna_service {

struct InstrumentError {
    int code = 0;
    std::string message = "No error";
};

}  // namespace vna_service

#endif
