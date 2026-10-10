/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_PARSER_H
#define VNAPRO_SCPI_PARSER_H
#include <string>
#include <vector>

#include "scpi/parsed_command.h"

namespace VnaScpi {
// Splits semicolon units while retaining delimiters inside SCPI quoted strings.
std::vector<std::string> SplitProgram(const std::string &input);
// Updates the relative header parent; syntax failures are returned in the command.
ParsedCommand ParseCommand(const std::string &unit, std::vector<std::string> &parent);
// Converts header characters without passing signed char values to ctype.
std::string Upper(std::string value);
// Removes protocol whitespace, preserving embedded NUL bytes for validation.
std::string Trim(const std::string &value);
// Produces a SCPI string value, escaping quotes by doubling them.
std::string Quote(const std::string &value);
// Accepts finite numeric values; optional frequency suffixes are converted to Hz.
bool Numeric(const std::string &text, double &value, bool frequencyUnits = false);
// Accepts a finite integer in the inclusive range [0, maximum].
bool Integer(const std::string &text, unsigned &value, unsigned maximum);
}  // namespace VnaScpi
#endif
