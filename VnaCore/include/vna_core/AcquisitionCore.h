#ifndef VNAPRO_VNA_CORE_ACQUISITION_CORE_H
#define VNAPRO_VNA_CORE_ACQUISITION_CORE_H

#include <memory>
#include <string>

#include "vna_core/CoreTypes.h"
#include "vna_core/SweepPlan.h"

namespace vna_core {
namespace detail {
class AcquisitionCoreImpl;
class DeviceSession;
class CoreTestAccess;
}  // namespace detail

class AcquisitionCore {
public:
    explicit AcquisitionCore(const std::string &nativeLibraryPath);
    ~AcquisitionCore();

    AcquisitionCore(const AcquisitionCore &) = delete;
    AcquisitionCore &operator=(const AcquisitionCore &) = delete;
    AcquisitionCore(AcquisitionCore &&) = delete;
    AcquisitionCore &operator=(AcquisitionCore &&) = delete;

    Ticket initialize();
    Ticket configure(SweepPlan plan);
    Ticket start(RunMode mode);
    Ticket triggerOnce(std::uint64_t runId, std::uint64_t revision);
    Ticket stop(std::uint64_t runId);
    Ticket recover();
    CoreView readView() const;
    CommandResult close();  // Owner-thread operation; waits for all accepted work and joins.

private:
    friend class detail::CoreTestAccess;
    explicit AcquisitionCore(std::unique_ptr<detail::DeviceSession> device);
    std::unique_ptr<detail::AcquisitionCoreImpl> impl_;
};

}  // namespace vna_core

#endif
