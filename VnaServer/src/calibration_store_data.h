/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_SERVER_CALIBRATION_STORE_DATA_H
#define VNAPRO_SERVER_CALIBRATION_STORE_DATA_H

#include <cstdint>
#include <string>
#include <vector>

#include "vna_service/calibration.h"

namespace VnaServer {
namespace CalibrationStoreData {

void CheckPool(const std::vector<VnaService::CalibrationSetPtr> &sets, std::uint64_t selected);
void CheckDocumentIdentity(
    const VnaService::CalibrationSetPtr &set, std::uint64_t id, const std::string &file, bool internal);
bool Same(const VnaService::CalibrationSet &a, const VnaService::CalibrationSet &b);

}  // 命名空间 CalibrationStoreData
}  // 命名空间 VnaServer
#endif
