/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CALIBRATION_CALIBRATION_H
#define VNAPRO_VNA_CALIBRATION_CALIBRATION_H

#include <array>
#include <complex>
#include <memory>
#include <vector>

namespace VnaCalibration {

using Complex = std::complex<double>;

enum class CalibrationError {
    NONE,
    INVALID_INPUT,
    SINGULAR,
    NONFINITE_RESULT
};

enum class CalibrationMethod {
    ONE_PORT,
    TWO_PORT
};

struct OnePortTerms final {
    Complex directivity;
    Complex sourceMatch;
    Complex reflectionTracking;
};

struct TransmissionTerms final {
    Complex isolation;
    Complex loadMatch;
    Complex transmissionTracking;
};

struct TwoPortTerms final {
    OnePortTerms forwardReflection;
    OnePortTerms reverseReflection;
    TransmissionTerms forwardTransmission;
    TransmissionTerms reverseTransmission;
};

// 第一端口到第二端口为正向；字段顺序与 Snau 校正核心 [11,21,22,12] 一致。
struct TwoPortNetwork final {
    Complex s11;
    Complex s21;
    Complex s22;
    Complex s12;
};

// 同一不可变系数快照；ONE_PORT 仅填 onePortTerms，TWO_PORT 仅填 twoPortTerms。
struct CalibrationData final {
    CalibrationMethod method = CalibrationMethod::ONE_PORT;
    unsigned port1 = 1U;
    unsigned port2 = 0U;
    std::vector<double> frequencyHz;
    std::vector<OnePortTerms> onePortTerms;
    std::vector<TwoPortTerms> twoPortTerms;

    bool Covers(unsigned receiverPort, unsigned sourcePort) const noexcept;
};

using CalibrationPtr = std::shared_ptr<const CalibrationData>;

// 三个已知复数标准，调用者决定 O/S/L 次序；失败时不修改输出。
CalibrationError SolveThreeStandards(
    const std::array<Complex, 3U> &measured, const std::array<Complex, 3U> &standards, OnePortTerms &output) noexcept;

// 已知且可非互易的 THRU；隔离项须显式提供，失败时不修改输出。
CalibrationError SolveKnownThru(const TwoPortNetwork &measured, const TwoPortNetwork &defined,
    const OnePortTerms &forward, const OnePortTerms &reverse, const Complex &forwardIsolation,
    const Complex &reverseIsolation, TwoPortTerms &output) noexcept;

CalibrationError CorrectOnePort(const Complex &measured, const OnePortTerms &terms, Complex &output) noexcept;

// 四个原始比值必须来自同一完整帧的同频点，不支持独立单条传输校正。
CalibrationError CorrectTwoPort(
    const TwoPortNetwork &measured, const TwoPortTerms &terms, TwoPortNetwork &output) noexcept;

}  // 命名空间 VnaCalibration

#endif
