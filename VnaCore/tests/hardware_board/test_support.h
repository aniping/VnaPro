/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_HARDWARE_BOARD_TEST_SUPPORT_H
#define VNAPRO_HARDWARE_BOARD_TEST_SUPPORT_H

#include <string>
#include <vector>

#include "vna_core/acquisition_core.h"

namespace HardwareBoardTest {

constexpr std::size_t TEST_POINT_COUNT = 130U;

void Require(bool value, const std::string &message);
VnaCore::CommandResult Await(const VnaCore::Ticket &ticket);
VnaCore::CommandResult Success(const VnaCore::Ticket &ticket);
VnaCore::SweepPlan Plan(std::vector<std::uint32_t> sources, std::size_t points = TEST_POINT_COUNT);
void VerifyFrame(const VnaCore::FramePtr &frame, const VnaCore::SweepPlan &plan, std::uint32_t sweep);
void VerifyProfile(const std::string &library, const std::string &trace, std::vector<std::uint32_t> sources);
void VerifyPowerDbmContract(const std::string &library, const std::string &trace);
void VerifyCloseWaits(const std::string &library, const std::string &trace);
void VerifyContinuousFrames(const std::string &library, const std::string &trace);
void VerifyIdleRecovery(const std::string &library, const std::string &trace);
void VerifyFaults(const std::string &library, const std::string &trace);
void VerifyFailedRfOff(const std::string &library, const std::string &trace);
void VerifyUnknownStatus(const std::string &library, const std::string &trace);

}  // 命名空间 HardwareBoardTest

#endif
