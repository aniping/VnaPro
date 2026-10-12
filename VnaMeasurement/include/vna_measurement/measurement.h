/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_MEASUREMENT_MEASUREMENT_H
#define VNAPRO_VNA_MEASUREMENT_MEASUREMENT_H

#include <string>

namespace VnaMeasurement {

enum class MeasurementKind {
    RECEIVER,
    S_PARAMETER
};

struct Measurement final {
    std::string name;
    MeasurementKind kind = MeasurementKind::RECEIVER;
    char receiverKind = 'a';
    unsigned receiverPort = 1U;
    unsigned sourcePort = 1U;

    // 返回 Keysight 兼容参数名；S 参数仅表示未校准的波量比值。
    std::string Parameter() const;
    // 原子解析受支持的标准参数名，各控制协议共享端口映射和参数规则。
    bool SetParameter(const std::string &parameter);

    // 检查当前计算模块支持的波量与端口组合，不检查用户测量名称。
    bool IsSupported() const noexcept;
};

}  // 命名空间 VnaMeasurement

#endif
