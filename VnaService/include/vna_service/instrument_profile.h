/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_SERVICE_INSTRUMENT_PROFILE_H
#define VNAPRO_VNA_SERVICE_INSTRUMENT_PROFILE_H

#include <cstdint>

namespace VnaService {

constexpr unsigned MIN_SWEEP_POINTS = 2U;
constexpr unsigned MAX_SWEEP_POINTS = 10001U;

// 启动时确定的仪器能力及默认设置，运行期间不随测量配置变化。
struct InstrumentProfile final {
    unsigned portCount = 4U;
    double minFrequencyHz = 10000000.0;
    double maxFrequencyHz = 8000000000.0;
    double defaultStartHz = 10000000.0;
    double defaultStopHz = 8000000000.0;
    unsigned defaultPoints = 201U;
    double defaultIfBandwidthHz = 1000.0;
    // VN10 的逐点原始功率编码，不解释为 dBm；0 是已确认的合法启动默认值。
    std::uint64_t defaultPowerRaw = 0U;
};

// 校验能力与默认设置的一致性；无效配置抛出 invalid_argument，禁止启动。
void ValidateInstrumentProfile(const InstrumentProfile &profile);

}  // 命名空间 VnaService

#endif
