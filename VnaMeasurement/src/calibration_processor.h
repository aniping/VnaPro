/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_MEASUREMENT_CALIBRATION_PROCESSOR_H
#define VNAPRO_VNA_MEASUREMENT_CALIBRATION_PROCESSOR_H

#include <array>

#include "vna_measurement/measurement_processor.h"

namespace VnaMeasurement {
namespace Detail {

std::string ValidateCalibrationMetadata(const VnaCalibration::CalibrationData &calibration);

std::size_t PairIndex(const Measurement &measurement, const VnaCalibration::CalibrationData &calibration) noexcept;

std::array<TraceResult, 4U> CalculatePair(
    const VnaCore::MeasurementFrame &frame, const VnaCalibration::CalibrationData &calibration);

std::array<TraceResult, 16U> CalculateFourPort(
    const VnaCore::MeasurementFrame &frame, const VnaCalibration::CalibrationData &calibration);

TraceResult ApplyCalibration(const VnaCore::MeasurementFrame &frame, const Measurement &measurement,
    const VnaCalibration::CalibrationData &calibration, TraceResult uncalibrated);

}  // 命名空间 Detail
}  // 命名空间 VnaMeasurement

#endif
