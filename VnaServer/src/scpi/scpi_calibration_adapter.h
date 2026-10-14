/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SCPI_CALIBRATION_ADAPTER_H
#define VNAPRO_SCPI_CALIBRATION_ADAPTER_H

#include "scpi/parsed_command.h"
#include "vna_service/instrument_service.h"

namespace VnaScpi {

struct ScpiCalibrationResult final {
    bool matched = false;
    int errorCode = 0;
    std::string error;
    std::string response;
    VnaService::Operation operation;
};

// 只映射校准命令，标准采集与原子系数切换复用仪器服务。
class ScpiCalibrationAdapter final {
public:
    explicit ScpiCalibrationAdapter(VnaService::InstrumentService &service);
    ScpiCalibrationResult Execute(const ParsedCommand &command) const;

private:
    ScpiCalibrationResult Collection(const ParsedCommand &command) const;
    ScpiCalibrationResult Sets(const ParsedCommand &command) const;
    VnaService::InstrumentService &service_;
};

}  // 命名空间 VnaScpi
#endif
