/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_QUERY_POLICY_H
#define VNAPRO_SCPI_QUERY_POLICY_H

#include "scpi/parsed_command.h"

namespace VnaScpi {

// 观察连接只接受无仪器副作用的已实现查询；读清寄存器与错误出队不属于只读。
bool IsObserverQuery(const ParsedCommand &command);

}  // 命名空间 VnaScpi
#endif
