#ifndef VNAPRO_CORE_TEST_RELEASE_GATE_ON_EXIT_H
#define VNAPRO_CORE_TEST_RELEASE_GATE_ON_EXIT_H

#include "controlled_device.h"

namespace vna_core_test {

struct ReleaseGateOnExit {
    ControlledDevice *device;

    ~ReleaseGateOnExit()
    {
        device->releaseGate();
    }
};

}  // namespace vna_core_test

#endif
