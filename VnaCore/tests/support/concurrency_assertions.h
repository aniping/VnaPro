/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_CONCURRENCY_ASSERTIONS_H
#define VNAPRO_CORE_CONCURRENCY_ASSERTIONS_H

#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

#include "vna_core/core_types.h"
#include "vna_core/measurement_frame.h"
#include "vna_core/sweep_plan.h"

namespace vna_core_test {

using vna_core::CommandResult;
using vna_core::MeasurementFrame;
using vna_core::SweepPlan;
using vna_core::Ticket;

void Require(bool value, const char *message);

CommandResult Await(const Ticket &ticket);

SweepPlan MakePlan(std::uint32_t variant = 0U);

bool SamePlan(const SweepPlan &left, const SweepPlan &right);

double SampleValue(
    const SweepPlan &plan, std::uint64_t frame, std::uint32_t source, std::size_t point, std::size_t slot);

void RequireFrameContent(const MeasurementFrame &frame);

void RequireUnchanged(const MeasurementFrame &frame, const MeasurementFrame &snapshot);

}  // namespace vna_core_test

#endif
