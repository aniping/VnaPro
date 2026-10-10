#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_CONFIGPATCH_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_CONFIGPATCH_H

namespace vna_service {

struct ConfigPatch {
    bool hasStartHz = false;
    bool hasStopHz = false;
    bool hasPoints = false;
    bool hasIfBandwidthHz = false;
    double startHz = 0;
    double stopHz = 0;
    unsigned points = 0;
    double ifBandwidthHz = 0;
};

}  // namespace vna_service

#endif
