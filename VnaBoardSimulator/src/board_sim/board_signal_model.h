/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_SIGNAL_MODEL_H
#define VNAPRO_BOARD_SIGNAL_MODEL_H

#include "board_sweep_types.h"
#include "vna_board/board_api_contract.h"

namespace VnaBoard {

class BoardSignalModel final {
public:
    // 固定扫描编号使同一噪声扫描可分段复现。
    BoardSignalModel(SignalSettings settings, std::uint64_t sweepNumber);
    BoardSignalModel(const BoardSignalModel &) = delete;
    BoardSignalModel &operator=(const BoardSignalModel &) = delete;
    BoardSignalModel(BoardSignalModel &&) = delete;
    BoardSignalModel &operator=(BoardSignalModel &&) = delete;
    // 一次生成一个源端口对应的四个接收端口复数波。
    void GenerateSourcePoint(const SweepPlan &plan, std::size_t point, std::uint32_t source, double *output) const;

private:
    double Noise(std::size_t point, std::uint32_t source, std::uint32_t receiver, std::uint32_t component) const;

    SignalSettings settings_;
    std::uint64_t sweepNumber_;
};

}  // 命名空间 VnaBoard
#endif  // VNAPRO_BOARD_SIGNAL_MODEL_H
