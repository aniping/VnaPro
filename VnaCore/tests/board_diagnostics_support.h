/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_DIAGNOSTICS_SUPPORT_H
#define VNAPRO_BOARD_DIAGNOSTICS_SUPPORT_H

#include <cstdint>
#include <string>
#include <vector>

#include "vna_log/log_config.h"

namespace BoardDiagnosticsTest {
using Records = std::vector<std::string>;

class Capture final {
public:
    Capture(std::string path, VnaLog::Level level);
    ~Capture();
    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;
    Records Read() const;

private:
    std::string path_;
};

struct Summary final {
    std::string name;
    std::string element;
    std::string unit;
    std::vector<double> values;
};

std::string Field(const std::string &record, const std::string &key);
std::size_t Find(const Records &records, const std::vector<std::string> &needles);
void VerifyStatus(const std::string &record, std::uint32_t status);
void VerifySummary(const Records &records, std::uint64_t config, const Summary &expected);
void VerifyValues(const Records &records, std::uint64_t config, const Summary &expected);
void VerifyPureExtremes(const std::string &prefix);
}  // 命名空间 BoardDiagnosticsTest
#endif
