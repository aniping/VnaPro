#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSNAPSHOT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTSNAPSHOT_H
#include <string>

#include "vna_core/CoreView.h"
#include "vna_service/InstrumentSettings.h"

namespace vna_service {

struct InstrumentSnapshot {
    InstrumentSettings settings;
    vna_core::CoreView core;
    std::string controlOwner;
    bool releasing = false;
    bool pending = false;
    bool finitePending = false;
};

}  // namespace vna_service

#endif
