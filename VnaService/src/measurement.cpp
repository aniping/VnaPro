/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_service/measurement.h"

namespace VnaService {

std::string Measurement::Parameter() const
{
    return std::string(1, receiverKind) + std::to_string(receiverPort) + "," + std::to_string(sourcePort);
}

}  // namespace VnaService
