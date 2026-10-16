/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_CALIBRATION_STORE_TEST_SUPPORT_H
#define VNAPRO_CALIBRATION_STORE_TEST_SUPPORT_H

#include <cstdint>
#include <string>

#include "vna_service/calibration.h"

namespace VnaServer {
class CalibrationStore;
}

namespace VnaServerTest {
void Require(bool value, const std::string &message);
#ifdef _WIN32
std::wstring Native(const std::string &path);
#else
const std::string &Native(const std::string &path);
#endif
std::string Read(const std::string &path);
void Write(const std::string &path, const std::string &text);
void Remove(const std::string &path);
void Rename(const std::string &from, const std::string &to);
void MakeDirectory(const std::string &directory);
void RemoveTestDirectory(const std::string &directory);
VnaService::CalibrationSetPtr Set(std::uint64_t id, bool four);
void CheckHotSelectionWithoutReads(
    VnaServer::CalibrationStore &store, const std::string &directory, const VnaService::CalibrationSetPtr &changed);
}  // 命名空间 VnaServerTest
#endif
