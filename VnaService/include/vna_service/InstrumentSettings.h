#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSETTINGS_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSETTINGS_H
#include <cstdint>
#include <string>
#include <vector>

#include "vna_service/Measurement.h"
#include "vna_service/TriggerTypes.h"

namespace vna_service {

struct InstrumentSettings {
    double startHz = 1000000.0;
    double stopHz = 100000000.0;
    unsigned points = 201U;
    double ifBandwidthHz = 1000.0;
    TriggerSource triggerSource = TriggerSource::Immediate;
    SweepMode sweepMode = SweepMode::Hold;
    bool currentTriggerScope = false;
    std::vector<Measurement> measurements;
    std::string selectedMeasurement;
    std::uint64_t revision = 0U;
};

}  // namespace vna_service

#endif
