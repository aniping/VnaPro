/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#include <string>

namespace VnaService {

struct Measurement final {
    std::string name;
    char receiverKind = 'a';
    unsigned receiverPort = 1U;
    unsigned sourcePort = 1U;
    // 返回兼容 Keysight 的非比值接收机参数，例如 a1,1。
    std::string Parameter() const;
};

}  // 命名空间 VnaService

#endif
