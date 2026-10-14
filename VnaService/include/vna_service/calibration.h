/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_SERVICE_CALIBRATION_H
#define VNAPRO_VNA_SERVICE_CALIBRATION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vna_calibration/calibration.h"
#include "vna_measurement/measurement.h"
#include "vna_measurement/trace_result.h"

namespace VnaService {

constexpr std::size_t MAX_CALIBRATION_SETS = 16U;

enum class CalibrationStandard {
    OPEN,
    SHORT,
    LOAD,
    THRU,
    ISOLATION
};
enum class CalibrationChangeKind {
    BEGIN,
    CAPTURE,
    FINISH,
    CANCEL,
    ENABLE,
    SELECT,
    DELETE_SET,
    IMPORT_SET
};

// 系数集冻结实际刺激与设备身份；数学系数本身不引用服务或驱动。
struct CalibrationSet final {
    std::uint64_t id = 0U;
    std::string name;
    std::string deviceIdentity;
    bool includeIsolation = false;
    double ifBandwidthHz = 0.0;
    std::vector<std::uint64_t> powerRaw;
    VnaCalibration::CalibrationPtr data;
};

using CalibrationSetPtr = std::shared_ptr<const CalibrationSet>;

struct CalibrationChange final {
    CalibrationChangeKind kind = CalibrationChangeKind::BEGIN;
    VnaCalibration::CalibrationMethod method = VnaCalibration::CalibrationMethod::ONE_PORT;
    unsigned port1 = 1U;
    unsigned port2 = 0U;
    bool includeIsolation = false;
    std::string name;
    std::uint64_t sessionId = 0U;
    std::string stepId;
    std::uint64_t setId = 0U;
    bool enabled = false;
    CalibrationSetPtr importedSet;
};

struct CalibrationStep final {
    std::string id;
    CalibrationStandard kind = CalibrationStandard::OPEN;
    unsigned port1 = 1U;
    unsigned port2 = 0U;
    bool captured = false;
    std::uint64_t runId = 0U;
    std::uint64_t frameSeq = 0U;
};

struct CalibrationSetSummary final {
    std::uint64_t id = 0U;
    std::string name;
    VnaCalibration::CalibrationMethod method = VnaCalibration::CalibrationMethod::ONE_PORT;
    unsigned port1 = 1U;
    unsigned port2 = 0U;
    bool includeIsolation = false;
    bool compatible = false;
    double startHz = 0.0;
    double stopHz = 0.0;
    unsigned points = 0U;
    double ifBandwidthHz = 0.0;
};

struct CalibrationSnapshot final {
    std::uint64_t revision = 1U;
    bool collecting = false;
    bool capturePending = false;
    std::uint64_t sessionId = 0U;
    VnaCalibration::CalibrationMethod method = VnaCalibration::CalibrationMethod::ONE_PORT;
    unsigned port1 = 1U;
    unsigned port2 = 0U;
    bool includeIsolation = false;
    std::vector<CalibrationStep> steps;
    std::uint64_t selectedSetId = 0U;
    bool enabled = false;
    bool compatible = false;
    std::string reason;
    std::string error;
    std::vector<CalibrationSetSummary> sets;
};

// 已确认标准的只读原始向量，来源身份属于该标准，不冒充当前 DUT 帧。
struct CalibrationPreviewResult final {
    bool available = false;
    std::string error;
    std::uint64_t calibrationRevision = 0U;
    std::uint64_t sessionId = 0U;
    CalibrationStep step;
    std::vector<double> frequencyHz;
    double ifBandwidthHz = 0.0;
    VnaMeasurement::Measurement measurement;
    VnaMeasurement::TraceResult trace;
};

}  // 命名空间 VnaService
#endif
