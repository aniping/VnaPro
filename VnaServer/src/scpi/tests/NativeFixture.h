#ifndef VNAPRO_SCPI_TEST_NATIVE_FIXTURE_H
#define VNAPRO_SCPI_TEST_NATIVE_FIXTURE_H

#include <stdexcept>
#include <string>

#include "vna_core/AcquisitionCore.h"
#include "vna_service/InstrumentService.h"

namespace vna_scpi_test {

struct NativeFixture {
    vna_core::AcquisitionCore core;
    vna_service::InstrumentService service;

    explicit NativeFixture(const std::string& driver) : core(driver), service(core)
    {
        if (!core.initialize().get().ok) {
            throw std::runtime_error("initialize failed");
        }
        std::string error;
        if (!service.bootstrapMockProfile(error)) {
            throw std::runtime_error(error);
        }
    }

    ~NativeFixture()
    {
        core.close();
    }
};

}  // namespace vna_scpi_test

#endif
