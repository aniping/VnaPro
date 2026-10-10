/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_OPERATIONRESULT_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_OPERATIONRESULT_H
#include <future>
#include <string>

namespace vna_service {

struct OperationResult final {
    bool ok = false;
    int errorCode = 0;
    std::string message;
};

using Operation = std::shared_future<OperationResult>;

}  // namespace vna_service

#endif
