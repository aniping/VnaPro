#ifndef VNAPRO_CORE_TEST_FIXTURE_H
#define VNAPRO_CORE_TEST_FIXTURE_H

#include <memory>
#include <utility>

#include "ControlledDevice.h"
#include "CoreTestAccess.h"

namespace vna_core_test {

struct Fixture {
    ControlledDevice* device;
    std::unique_ptr<vna_core::AcquisitionCore> core;

    Fixture()
    {
        auto owned = std::unique_ptr<ControlledDevice>(new ControlledDevice);
        device = owned.get();
        core = vna_core::detail::CoreTestAccess::create(std::move(owned));
        require(await(core->initialize()).ok, "initialize failed");
        require(core->readView().deviceIdn == "TestDevice", "identity was not published");
        require(await(core->configure(makePlan())).ok, "configure failed");
    }

    ~Fixture()
    {
        device->releaseGate();
        core->close();
    }
};

}  // namespace vna_core_test

#endif
