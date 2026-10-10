/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_MEASUREMENT_H
#include <string>

namespace vna_service {

struct Measurement final {
    std::string name;
    char receiverKind = 'a';
    unsigned receiverPort = 1U;
    unsigned sourcePort = 1U;
    // Returns the Keysight-compatible non-ratio receiver parameter, such as a1,1.
    std::string Parameter() const;
};

}  // namespace vna_service

#endif
