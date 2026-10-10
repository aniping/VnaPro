#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_TRIGGERTYPES_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_TRIGGERTYPES_H

namespace vna_service {

enum class TriggerSource {
    Immediate,
    Manual
};
enum class SweepMode {
    Hold,
    Continuous,
    Single
};

}  // namespace vna_service

#endif
