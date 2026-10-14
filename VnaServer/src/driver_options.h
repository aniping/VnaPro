/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVER_DRIVER_OPTIONS_H
#define VNAPRO_SERVER_DRIVER_OPTIONS_H

#include <string>

#include "vna_core/device_session.h"

namespace VnaServer {

// 接口版本由启动配置明确选择；同名导出函数不能用于猜测驱动 ABI。
struct DriverOptions final {
    VnaCore::DriverApi api = VnaCore::DriverApi::TDC1VN10;
    std::string path;
};

}  // 命名空间 VnaServer

#endif
