/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTERROR_H
#define VNAPRO_VNASERVICE_INCLUDE_VNA_SERVICE_INSTRUMENTERROR_H
#include <string>

namespace VnaService {

struct InstrumentError final {
    int code = 0;
    std::string message = "No error";
};

}  // namespace VnaService

#endif
