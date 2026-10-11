/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_MEASUREMENT_MEASUREMENT_PROCESSOR_H
#define VNAPRO_VNA_MEASUREMENT_MEASUREMENT_PROCESSOR_H

#include "vna_core/measurement_frame.h"
#include "vna_measurement/measurement.h"
#include "vna_measurement/trace_result.h"

namespace VnaMeasurement {

// 原始参考波幅度的有效下限，单位 a.u.；幅度小于或等于该值时不能归一化。
constexpr double MIN_REFERENCE_MAGNITUDE = 1e-12;

class MeasurementProcessor final {
public:
    // 仅提供静态计算入口，不允许创建、复制或转移处理器实例。
    MeasurementProcessor() = delete;
    MeasurementProcessor(const MeasurementProcessor &) = delete;
    MeasurementProcessor &operator=(const MeasurementProcessor &) = delete;
    MeasurementProcessor(MeasurementProcessor &&) = delete;
    MeasurementProcessor &operator=(MeasurementProcessor &&) = delete;

    // 只读取同一完整不可变帧中的同源波量；结构错误不可用，坏采样点单独标记。
    static TraceResult Calculate(const VnaCore::MeasurementFrame &frame, const Measurement &measurement);
};

}  // 命名空间 VnaMeasurement

#endif
