/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <iostream>
#include <limits>

#include "calibration_applicability_fixture.h"

namespace CalibrationTest {
namespace {
Complex ReferencePolar(const Complex &lower, const Complex &upper)
{
    if (std::abs(lower) == 0.0 || std::abs(upper) == 0.0) {
        return (lower + upper) / 2.0;
    }
    return std::polar((std::abs(lower) + std::abs(upper)) / 2.0, std::arg(lower) + std::arg(upper / lower) / 2.0);
}

void Compare(const VnaCalibration::OnePortTerms &actual, const VnaCalibration::OnePortTerms &lower,
    const VnaCalibration::OnePortTerms &upper)
{
    Near(actual.directivity, ReferencePolar(lower.directivity, upper.directivity));
    Near(actual.sourceMatch, ReferencePolar(lower.sourceMatch, upper.sourceMatch));
    Near(actual.reflectionTracking, ReferencePolar(lower.reflectionTracking, upper.reflectionTracking));
}

void Compare(const VnaCalibration::TransmissionTerms &actual, const VnaCalibration::TransmissionTerms &lower,
    const VnaCalibration::TransmissionTerms &upper)
{
    Near(actual.isolation, ReferencePolar(lower.isolation, upper.isolation));
    Near(actual.loadMatch, ReferencePolar(lower.loadMatch, upper.loadMatch));
    Near(actual.transmissionTracking, ReferencePolar(lower.transmissionTracking, upper.transmissionTracking));
}

void CompareModels(const VnaCalibration::CalibrationData &actual, const VnaCalibration::CalibrationData &source)
{
    if (source.method == CalibrationMethod::ONE_PORT) {
        Compare(actual.onePortTerms[1U], source.onePortTerms[0U], source.onePortTerms[1U]);
    } else if (source.method == CalibrationMethod::TWO_PORT) {
        const auto &terms = actual.twoPortTerms[1U];
        const auto &first = source.twoPortTerms[0U];
        const auto &second = source.twoPortTerms[1U];
        Compare(terms.forwardReflection, first.forwardReflection, second.forwardReflection);
        Compare(terms.reverseReflection, first.reverseReflection, second.reverseReflection);
        Compare(terms.forwardTransmission, first.forwardTransmission, second.forwardTransmission);
        Compare(terms.reverseTransmission, first.reverseTransmission, second.reverseTransmission);
        Require(actual.port1 == 4U && actual.port2 == 2U, "Nonconsecutive physical port orientation changed");
    } else {
        for (std::size_t cell = 0U; cell < 16U; ++cell) {
            const auto &terms = actual.fourPortTerms[1U][cell];
            const auto &first = source.fourPortTerms[0U][cell];
            const auto &second = source.fourPortTerms[1U][cell];
            Near(terms.offset, ReferencePolar(first.offset, second.offset));
            Near(terms.match, ReferencePolar(first.match, second.match));
            Near(terms.tracking, ReferencePolar(first.tracking, second.tracking));
        }
    }
}

void TestAllModels()
{
    for (auto method : {CalibrationMethod::ONE_PORT, CalibrationMethod::TWO_PORT, CalibrationMethod::FOUR_PORT}) {
        const auto source = Set(method, {100, 200, 300, 400, 500});
        const auto result = VnaService::PrepareCalibration(source, IDENTITY, Plan({100, 150, 300, 450, 500}));
        Require(result.correction && result.applicability.prepared && result.applicability.exactPoints == 3U &&
                    result.applicability.interpolatedPoints == 2U,
            "Model interpolation preparation failed");
        CompareModels(*result.correction, *source->data);
        Require(source->data->frequencyHz == std::vector<double>{100, 200, 300, 400, 500}, "Source axis changed");
    }
}

void TestExactNegativeZero()
{
    const auto source = Set(CalibrationMethod::FOUR_PORT, {100, 200, 300, 400});
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*source->data);
    for (auto &terms : data->fourPortTerms) {
        for (auto &cell : terms) {
            cell.offset.real(-0.0);
            cell.match.imag(-0.0);
        }
    }
    source->data = data;
    const auto subset = VnaService::PrepareCalibration(source, IDENTITY, Plan({200, 400}));
    const auto mixed = VnaService::PrepareCalibration(source, IDENTITY, Plan({200, 250, 400}));
    Require(subset.correction && mixed.correction, "Negative zero fixture failed validation");
    for (std::size_t cell = 0U; cell < 16U; ++cell) {
        Exact(subset.correction->fourPortTerms[0U][cell].offset, data->fourPortTerms[1U][cell].offset);
        Exact(subset.correction->fourPortTerms[1U][cell].match, data->fourPortTerms[3U][cell].match);
        Exact(mixed.correction->fourPortTerms[0U][cell].offset, data->fourPortTerms[1U][cell].offset);
        Exact(mixed.correction->fourPortTerms[2U][cell].match, data->fourPortTerms[3U][cell].match);
    }
}

void TestPhaseAndZero()
{
    constexpr double PI = 3.14159265358979323846;
    const auto source = Set(CalibrationMethod::ONE_PORT, {100, 300});
    for (double angle : {60.0, 179.0}) {
        auto data = std::make_shared<VnaCalibration::CalibrationData>(*source->data);
        data->onePortTerms[0U].reflectionTracking = std::polar(1.0, -angle * PI / 180.0);
        data->onePortTerms[1U].reflectionTracking = std::polar(1.0, angle * PI / 180.0);
        data->onePortTerms[0U].directivity = {0.0, 0.0};
        data->onePortTerms[1U].directivity = {0.1, -0.2};
        source->data = data;
        const auto result = VnaService::PrepareCalibration(source, IDENTITY, Plan({100, 200, 300}));
        Require(result.correction && result.applicability.prepared, "Phase boundary preparation failed");
        const auto &middle = result.correction->onePortTerms[1U];
        Near(middle.reflectionTracking, angle == 60.0 ? Complex{1.0, 0.0} : Complex{-1.0, 0.0});
        Near(middle.directivity, {0.05, -0.1});
        Exact(
            result.correction->onePortTerms.front().reflectionTracking, data->onePortTerms.front().reflectionTracking);
    }
}

void TestDerivedMathRejection()
{
    const auto source = Set(CalibrationMethod::ONE_PORT, {100, 300});
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*source->data);
    data->onePortTerms[0U].reflectionTracking = {0.0, 0.0};
    data->onePortTerms[1U].reflectionTracking = {0.0, 0.0};
    source->data = data;
    CheckReason(VnaService::PrepareCalibration(source, IDENTITY, Plan({100, 200, 300})),
        CalibrationApplicabilityReason::INVALID_COEFFICIENTS);
    data->onePortTerms[0U].reflectionTracking = {1.0, 0.0};
    data->onePortTerms[1U].reflectionTracking = {1.0, 0.0};
    data->onePortTerms[0U].directivity = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    CheckReason(VnaService::PrepareCalibration(source, IDENTITY, Plan({100, 200, 300})),
        CalibrationApplicabilityReason::INVALID_COEFFICIENTS);
}

void TestOriginalSourceTransitions()
{
    const auto source = Set(CalibrationMethod::FOUR_PORT, {100, 200, 300, 400, 500});
    const auto original = source->data;
    const auto first = VnaService::PrepareCalibration(source, IDENTITY, Plan({100, 150, 250, 500}));
    const auto second = VnaService::PrepareCalibration(source, IDENTITY, Plan({200, 300, 400}));
    const auto final = VnaService::PrepareCalibration(source, IDENTITY, Plan({150, 350, 450}));
    const auto direct = VnaService::PrepareCalibration(source, IDENTITY, Plan({150, 350, 450}));
    Require(first.correction && second.correction && final.correction && direct.correction && source->data == original,
        "A to B to C mutated or replaced the source");
    for (std::size_t point = 0U; point < 3U; ++point) {
        for (std::size_t cell = 0U; cell < 16U; ++cell) {
            Exact(final.correction->fourPortTerms[point][cell].offset,
                direct.correction->fourPortTerms[point][cell].offset);
            Exact(final.correction->fourPortTerms[point][cell].match,
                direct.correction->fourPortTerms[point][cell].match);
            Exact(final.correction->fourPortTerms[point][cell].tracking,
                direct.correction->fourPortTerms[point][cell].tracking);
        }
    }
}

void MeasurePreparation()
{
    for (unsigned points : {800U, 4000U, 6700U}) {
        std::vector<double> sourceAxis;
        std::vector<double> targetAxis;
        for (unsigned point = 0U; point < points; ++point) {
            sourceAxis.push_back(10000000.0 + static_cast<double>(point) * 1000.0);
            if (point + 1U < points) {
                targetAxis.push_back(sourceAxis.back() + 500.0);
            }
        }
        const auto source = Set(CalibrationMethod::FOUR_PORT, sourceAxis);
        const auto start = std::chrono::steady_clock::now();
        const auto result = VnaService::PrepareCalibration(source, IDENTITY, Plan(targetAxis));
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Require(result.applicability.prepared, "Performance fixture preparation failed");
        std::cout << "PolarLinearV1 four-port " << points << " points prepareMs=" << elapsed << '\n';
    }
}
}  // 匿名命名空间

void RunPreparationTests()
{
    TestAllModels();
    TestExactNegativeZero();
    TestPhaseAndZero();
    TestDerivedMathRejection();
    TestOriginalSourceTransitions();
    MeasurePreparation();
}
}  // 命名空间 CalibrationTest
