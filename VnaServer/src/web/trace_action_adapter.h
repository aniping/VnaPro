/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_TRACE_ACTION_ADAPTER_H
#define VNAPRO_TRACE_ACTION_ADAPTER_H

#include <string>

#include "vna_service/instrument_service.h"
#include "web/json.h"

namespace VnaWeb {

// 原生图窗操作的协议边界；目录分配、复制及删除均交给 C++ 仪器事务。
class TraceActionAdapter final {
public:
    TraceActionAdapter() = delete;
    static VnaService::Operation Execute(
        VnaService::InstrumentService &service, const std::string &path, const Json &body, const std::string &owner);
};

}  // 命名空间 VnaWeb
#endif
