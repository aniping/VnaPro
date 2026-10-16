/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "calibration_coefficient_slots.h"

#include <cstddef>

namespace VnaService {
namespace Detail {

std::array<VnaCalibration::Complex, 3U> CoefficientSlots(const VnaCalibration::OnePortTerms &terms)
{
    return {{terms.directivity, terms.sourceMatch, terms.reflectionTracking}};
}

void SetCoefficientSlots(const std::array<VnaCalibration::Complex, 3U> &values, VnaCalibration::OnePortTerms &terms)
{
    terms = {values[0U], values[1U], values[2U]};
}

std::array<VnaCalibration::Complex, 12U> CoefficientSlots(const VnaCalibration::TwoPortTerms &terms)
{
    return {{terms.forwardReflection.directivity, terms.forwardReflection.sourceMatch,
        terms.forwardReflection.reflectionTracking, terms.reverseReflection.directivity,
        terms.reverseReflection.sourceMatch, terms.reverseReflection.reflectionTracking,
        terms.forwardTransmission.isolation, terms.forwardTransmission.loadMatch,
        terms.forwardTransmission.transmissionTracking, terms.reverseTransmission.isolation,
        terms.reverseTransmission.loadMatch, terms.reverseTransmission.transmissionTracking}};
}

void SetCoefficientSlots(const std::array<VnaCalibration::Complex, 12U> &values, VnaCalibration::TwoPortTerms &terms)
{
    terms.forwardReflection = {values[0U], values[1U], values[2U]};
    terms.reverseReflection = {values[3U], values[4U], values[5U]};
    terms.forwardTransmission = {values[6U], values[7U], values[8U]};
    terms.reverseTransmission = {values[9U], values[10U], values[11U]};
}

std::array<VnaCalibration::Complex, 48U> CoefficientSlots(const VnaCalibration::FourPortTerms &terms)
{
    std::array<VnaCalibration::Complex, 48U> result;
    for (std::size_t cell = 0U; cell < terms.size(); ++cell) {
        result[cell * 3U] = terms[cell].offset;
        result[cell * 3U + 1U] = terms[cell].match;
        result[cell * 3U + 2U] = terms[cell].tracking;
    }
    return result;
}

void SetCoefficientSlots(const std::array<VnaCalibration::Complex, 48U> &values, VnaCalibration::FourPortTerms &terms)
{
    for (std::size_t cell = 0U; cell < terms.size(); ++cell) {
        terms[cell] = {values[cell * 3U], values[cell * 3U + 1U], values[cell * 3U + 2U]};
    }
}
}  // 命名空间 Detail
}  // 命名空间 VnaService
