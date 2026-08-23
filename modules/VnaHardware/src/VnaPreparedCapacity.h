#ifndef VNA_HARDWARE_VNA_PREPARED_CAPACITY_H
#define VNA_HARDWARE_VNA_PREPARED_CAPACITY_H

#include <cstddef>

#include "Result.h"
#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaSweepPlan.h"

namespace vna::detail {

Result<std::size_t> inspectRequestedPlanPayload(const VnaSweepPlan &plan, std::size_t maximumBytes);
Result<std::size_t> inspectDriverPreparedPayload(
    const VnaSweepPlan &plan, const VnaPrepareResultViewV1 &view, std::size_t maximumBytes);
Result<std::size_t> inspectPreparedSweepPayload(const PreparedSweep &prepared, std::size_t maximumBytes);

}  // namespace vna::detail

#endif
