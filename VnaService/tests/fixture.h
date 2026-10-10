#ifndef VNAPRO_SERVICE_TEST_FIXTURE_H
#define VNAPRO_SERVICE_TEST_FIXTURE_H

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "controlled_device.h"
#include "support/core_test_access.h"
#include "vna_service/instrument_service.h"

namespace vna_service_test {

struct Fixture {
    ControlledDevice *device;
    std::unique_ptr<vna_core::AcquisitionCore> core;
    std::unique_ptr<vna_service::InstrumentService> service;

    Fixture()
    {
        std::unique_ptr<ControlledDevice> owned(new ControlledDevice);
        device = owned.get();
        core = vna_core::detail::CoreTestAccess::create(std::move(owned));
        if (!core->initialize().get().ok) {
            throw std::runtime_error("controlled initialization");
        }
        service.reset(new vna_service::InstrumentService(*core));
        std::string error;
        if (!service->bootstrapMockProfile(error)) {
            throw std::runtime_error(error);
        }
    }

    ~Fixture()
    {
        device->release();
        core->close();
    }
};

}  // namespace vna_service_test

#endif
