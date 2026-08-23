#ifndef VNA_ACQUISITION_ACQUISITION_CAPACITY_H
#define VNA_ACQUISITION_ACQUISITION_CAPACITY_H

#include <cstddef>

#include "PreparedMapping.h"

namespace vna::detail {

struct AcquisitionShape {
    std::size_t pointExcitationRecordCount{0};
    std::size_t waveSampleCount{0};
    std::size_t layoutCount{0};
    std::size_t waveMappingCount{0};
};

bool checkedAdd(std::size_t left, std::size_t right, std::size_t &result) noexcept;
bool checkedMultiply(std::size_t left, std::size_t right, std::size_t &result) noexcept;
bool checkedAccumulate(std::size_t value, std::size_t &total) noexcept;
VnaError acquisitionError(VnaErrorCode code, const char *description);
Result<AcquisitionPlanInspection> inspectAcquisitionShape(
    const AcquisitionShape &shape, const AcquisitionLimits &limits);

}  // namespace vna::detail

#endif
