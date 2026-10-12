/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_DEVICE_SESSION_FACTORY_H
#define VNAPRO_VNA_CORE_DEVICE_SESSION_FACTORY_H

#include <memory>
#include <string>

#include "vna_core/device_session.h"

namespace VnaCore {
namespace Detail {

// 在各自的翻译单元构造适配器，避免两种同名 C 函数的不同原型进入同一编译单元。
std::unique_ptr<DeviceSession> CreateNativeDevice(const std::string &path);
std::unique_ptr<DeviceSession> CreateBoardDevice(const std::string &path);

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
