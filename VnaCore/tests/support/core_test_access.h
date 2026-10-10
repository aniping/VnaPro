#ifndef VNAPRO_CORE_TEST_ACCESS_H
#define VNAPRO_CORE_TEST_ACCESS_H

#include <memory>
#include <utility>

#include "device/device_session.h"
#include "vna_core/acquisition_core.h"

namespace vna_core {
namespace detail {

class CoreTestAccess {
public:
    static std::unique_ptr<AcquisitionCore> create(std::unique_ptr<DeviceSession> device)
    {
        return std::unique_ptr<AcquisitionCore>(new AcquisitionCore(std::move(device)));
    }
};

}  // namespace detail
}  // namespace vna_core

#endif
