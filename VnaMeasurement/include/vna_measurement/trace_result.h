/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_MEASUREMENT_TRACE_RESULT_H
#define VNAPRO_VNA_MEASUREMENT_TRACE_RESULT_H

#include <string>
#include <vector>

namespace VnaMeasurement {

enum class PointError {
    NONE,
    INVALID_SAMPLE,
    REFERENCE_TOO_SMALL,
    NONFINITE_RESULT
};

struct ComplexPoint final {
    double real = 0.0;
    double imag = 0.0;
    bool valid = false;
    PointError error = PointError::NONE;
};

struct TraceResult final {
    bool available = false;
    std::string error;
    std::vector<ComplexPoint> points;
};

}  // 命名空间 VnaMeasurement

#endif
