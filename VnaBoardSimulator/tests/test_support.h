/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_TEST_SUPPORT_H
#define VNAPRO_BOARD_TEST_SUPPORT_H

#include <string>
#include <vector>

#include "board_simulator_state.h"

namespace BoardTest {

void Require(bool condition, const std::string &message);
void SetEnvironment(const char *name, const char *value);
void Prepare(VnaBoard::BoardSimulatorState &state, std::size_t points, const std::vector<std::uint32_t> &ports = {0U},
    std::uint64_t firstFrequency = 1000000000ULL);
std::vector<double> Read(VnaBoard::BoardSimulatorState &state, std::uint32_t points, std::size_t sourceCount = 1U);
void TestState();
void TestAsync();

}  // 命名空间 BoardTest
#endif  // VNAPRO_BOARD_TEST_SUPPORT_H
