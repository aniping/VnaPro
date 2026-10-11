/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_measurement/measurement.h"

#include "vna_core/frame_layout.h"

namespace VnaMeasurement {

std::string Measurement::Parameter() const
{
    if (kind == MeasurementKind::S_PARAMETER) {
        return "S" + std::to_string(receiverPort) + std::to_string(sourcePort);
    }
    return std::string(1U, receiverKind) + std::to_string(receiverPort) + "," + std::to_string(sourcePort);
}

bool Measurement::IsSupported() const noexcept
{
    if (kind == MeasurementKind::S_PARAMETER) {
        return receiverKind == 'b' && sourcePort >= 1U && sourcePort <= 2U && receiverPort >= 1U && receiverPort <= 2U;
    }
    return kind == MeasurementKind::RECEIVER && (receiverKind == 'a' || receiverKind == 'b') && receiverPort >= 1U &&
           receiverPort <= VnaCore::PHYSICAL_PORT_COUNT && sourcePort >= 1U &&
           sourcePort <= VnaCore::PHYSICAL_PORT_COUNT;
}

}  // 命名空间 VnaMeasurement
