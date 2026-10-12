/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_PARSER_H
#define VNAPRO_SCPI_PARSER_H
#include <string>
#include <vector>

#include "scpi/parsed_command.h"

namespace VnaScpi {
// 按分号拆分程序单元，保留 SCPI 引号字符串内部的分隔符。
std::vector<std::string> SplitProgram(const std::string &input);
// 更新相对命令头的父路径；语法错误通过解析结果返回。
ParsedCommand ParseCommand(const std::string &unit, std::vector<std::string> &parent);
// 转换命令头字符，避免向 ctype 函数传入负值的 char。
std::string Upper(std::string value);
// 去除协议空白字符，保留内嵌 NUL 字节供后续校验。
std::string Trim(const std::string &value);
// 生成 SCPI 字符串值，通过重复引号转义字符串内部的引号。
std::string Quote(const std::string &value);
// 解码完整的单引号或双引号字符串；引号字符只能按 SCPI 重复转义。
bool StringArgument(const std::string &text, std::string &value);
// 仅接受有限数值；将可选的频率单位后缀换算为 Hz。
bool Numeric(const std::string &text, double &value, bool frequencyUnits = false);
// 仅接受闭区间 [0, maximum] 内的有限整数。
bool Integer(const std::string &text, unsigned &value, unsigned maximum);
}  // 命名空间 VnaScpi
#endif
