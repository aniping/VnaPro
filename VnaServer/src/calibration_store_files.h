/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_CALIBRATION_STORE_FILES_H
#define VNAPRO_CALIBRATION_STORE_FILES_H

#include <string>

namespace VnaServer {
namespace CalibrationStoreFiles {
bool InternalFile(const std::string &name, const std::string &prefix, const std::string &ending);
// 命名池仅引用单个安全 basename；channel- 前缀保留给服务内部的事务文件。
bool NamedFile(const std::string &name);
}  // 命名空间 CalibrationStoreFiles
}  // 命名空间 VnaServer
#endif
