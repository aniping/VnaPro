/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_WEB_CALIBRATION_FILE_JSON_H
#define VNAPRO_WEB_CALIBRATION_FILE_JSON_H

#include <string>

#include "vna_service/calibration.h"
#include "web/json.h"

namespace VnaWeb {
// 文件在 HTTP 中使用有界封套；落盘、下载的内容仍为二进制或 CSV。
class CalibrationFileJson final {
public:
    CalibrationFileJson() = delete;
    // format 仅 binary/csv；返回含编码、文件名、原字节数和内容的封套，无效集抛 invalid_argument。
    static Json Export(const VnaService::CalibrationSet &set, const std::string &format);
    // body 必须含匹配的 format/encoding/content；严格解码后返回完整集，非法文件抛 invalid_argument。
    static VnaService::CalibrationSetPtr Import(const Json &body);
};
}  // 命名空间 VnaWeb
#endif
