/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_DATARESULT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_DATARESULT_H
#include <string>
#include <vector>

namespace vna_service {

struct DataResult final {
    bool ok = false;
    std::string error;
    std::vector<double> values;
};

}  // namespace vna_service

#endif
