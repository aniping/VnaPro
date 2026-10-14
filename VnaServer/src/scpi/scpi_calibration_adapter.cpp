/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "scpi/scpi_calibration_adapter.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "scpi/scpi_constants.h"
#include "scpi/scpi_parser.h"

namespace VnaScpi {
namespace {
struct ParameterCountError final {
    int code;
};

ScpiCalibrationResult Handled()
{
    ScpiCalibrationResult result;
    result.matched = true;
    return result;
}

void Count(const ParsedCommand &command, std::size_t minimum, std::size_t maximum)
{
    if (command.arguments.size() < minimum || command.arguments.size() > maximum) {
        throw ParameterCountError{command.arguments.size() < minimum ? MISSING_PARAMETER : PARAMETER_NOT_ALLOWED};
    }
}

std::string Value(const std::string &argument)
{
    std::string result;
    if (!argument.empty() && (argument.front() == '\'' || argument.front() == '"')) {
        if (!StringArgument(argument, result)) {
            throw std::invalid_argument("Invalid quoted calibration argument");
        }
        return result;
    }
    return argument;
}

std::uint64_t Identifier(const std::string &argument)
{
    const std::string value = Value(argument);
    if (value.empty()) {
        throw std::invalid_argument("Calibration identifier is empty");
    }
    std::uint64_t number = 0U;
    for (const char digit : value) {
        if (digit < '0' || digit > '9' ||
            number > ((std::numeric_limits<std::uint64_t>::max)() - static_cast<unsigned>(digit - '0')) / 10U) {
            throw std::invalid_argument("Calibration identifier must be a decimal uint64");
        }
        number = number * 10U + static_cast<unsigned>(digit - '0');
    }
    return number;
}

unsigned Port(const std::string &argument)
{
    const auto number = Identifier(argument);
    if (number < 1U || number > RECEIVER_PORT_MAXIMUM) {
        throw std::invalid_argument("Calibration port is outside instrument range");
    }
    return static_cast<unsigned>(number);
}

bool Boolean(const std::string &argument)
{
    const std::string value = Upper(Value(argument));
    if (value == "1" || value == "ON") {
        return true;
    }
    if (value == "0" || value == "OFF") {
        return false;
    }
    throw std::invalid_argument("Calibration state must be ON/OFF or 1/0");
}

VnaService::CalibrationStandard Standard(const std::string &argument)
{
    const std::string value = Upper(Value(argument));
    if (value == "OPEN") {
        return VnaService::CalibrationStandard::OPEN;
    }
    if (value == "SHOR" || value == "SHORT") {
        return VnaService::CalibrationStandard::SHORT;
    }
    if (value == "LOAD" || value == "MATC" || value == "MATCH") {
        return VnaService::CalibrationStandard::LOAD;
    }
    if (value == "THR" || value == "THRU" || value == "THROUGH") {
        return VnaService::CalibrationStandard::THRU;
    }
    if (value == "ISOL" || value == "ISOLATION") {
        return VnaService::CalibrationStandard::ISOLATION;
    }
    throw std::invalid_argument("Unsupported calibration standard");
}
}  // 匿名命名空间

ScpiCalibrationAdapter::ScpiCalibrationAdapter(VnaService::InstrumentService &service) : service_(service)
{
}

ScpiCalibrationResult ScpiCalibrationAdapter::Execute(const ParsedCommand &command) const
{
    if (command.path.compare(0U, 10U, "SENS1:CORR") != 0) {
        return {};
    }
    try {
        if (command.path == "SENS1:CORR" || command.path == "SENS1:CORR:STAT") {
            auto result = Handled();
            if (command.query) {
                Count(command, 0U, 0U);
                const auto snapshot = service_.Calibration();
                result.response = snapshot.enabled && snapshot.compatible ? "1" : "0";
            } else {
                Count(command, 1U, 1U);
                VnaService::CalibrationChange change;
                change.kind = VnaService::CalibrationChangeKind::ENABLE;
                change.enabled = Boolean(command.arguments.front());
                result.operation = service_.ApplyCalibration(std::move(change));
            }
            return result;
        }
        if (command.path.compare(0U, 15U, "SENS1:CORR:COLL") == 0) {
            return Collection(command);
        }
        return Sets(command);
    } catch (const ParameterCountError &error) {
        auto result = Handled();
        result.errorCode = error.code;
        result.error = "Calibration parameter count mismatch";
        return result;
    } catch (const std::invalid_argument &error) {
        auto result = Handled();
        result.errorCode = ILLEGAL_PARAMETER_VALUE;
        result.error = error.what();
        return result;
    }
}

ScpiCalibrationResult ScpiCalibrationAdapter::Collection(const ParsedCommand &command) const
{
    const auto snapshot = service_.Calibration();
    if (command.path == "SENS1:CORR:COLL:STAT" && command.query) {
        Count(command, 0U, 0U);
        auto result = Handled();
        result.response = snapshot.capturePending ? "BUSY" : snapshot.collecting ? "READY" : "IDLE";
        return result;
    }
    if (command.query) {
        return {};
    }
    VnaService::CalibrationChange change;
    change.sessionId = snapshot.sessionId;
    if (command.path == "SENS1:CORR:COLL:METH" || command.path == "SENS1:CORR:COLL:METH:DEF") {
        Count(command, 2U, 4U);
        const std::string method = Upper(Value(command.arguments[0]));
        change.kind = VnaService::CalibrationChangeKind::BEGIN;
        change.port1 = Port(command.arguments[1]);
        if (method == "OSM" || method == "OSL") {
            Count(command, 2U, 2U);
            change.method = VnaCalibration::CalibrationMethod::ONE_PORT;
        } else if (method == "TOSM" || method == "SOLT") {
            Count(command, 3U, 4U);
            change.method = VnaCalibration::CalibrationMethod::TWO_PORT;
            change.port2 = Port(command.arguments[2]);
            change.includeIsolation = command.arguments.size() == 4U && Boolean(command.arguments[3]);
        } else {
            throw std::invalid_argument("Only mechanical OSM and TOSM calibration are available");
        }
    } else if (command.path == "SENS1:CORR:COLL:ACQ") {
        Count(command, 2U, 3U);
        change.kind = VnaService::CalibrationChangeKind::CAPTURE;
        const auto standard = Standard(command.arguments[0]);
        const unsigned port1 = Port(command.arguments[1]);
        const bool pair =
            standard == VnaService::CalibrationStandard::THRU || standard == VnaService::CalibrationStandard::ISOLATION;
        Count(command, pair ? 3U : 2U, pair ? 3U : 2U);
        const unsigned port2 = pair ? Port(command.arguments[2]) : 0U;
        const auto step = std::find_if(snapshot.steps.begin(), snapshot.steps.end(), [&](const auto &item) {
            return item.kind == standard && ((item.port1 == port1 && item.port2 == port2) ||
                                                (pair && item.port1 == port2 && item.port2 == port1));
        });
        if (step == snapshot.steps.end()) {
            throw std::invalid_argument("The standard is not required by the current calibration session");
        }
        change.stepId = step->id;
    } else if (command.path == "SENS1:CORR:COLL:SAVE") {
        Count(command, 0U, 1U);
        change.kind = VnaService::CalibrationChangeKind::FINISH;
        if (!command.arguments.empty()) {
            change.name = Value(command.arguments[0]);
        }
    } else if (command.path == "SENS1:CORR:COLL:ABOR") {
        Count(command, 0U, 0U);
        change.kind = VnaService::CalibrationChangeKind::CANCEL;
    } else {
        return {};
    }
    auto result = Handled();
    result.operation = service_.ApplyCalibration(std::move(change));
    return result;
}

ScpiCalibrationResult ScpiCalibrationAdapter::Sets(const ParsedCommand &command) const
{
    const auto snapshot = service_.Calibration();
    if (command.path == "SENS1:CORR:CSET:CAT" && command.query) {
        Count(command, 0U, 0U);
        auto result = Handled();
        for (const auto &set : snapshot.sets) {
            if (!result.response.empty()) {
                result.response += ',';
            }
            result.response += Quote(std::to_string(set.id)) + ',' + Quote(set.name);
        }
        if (result.response.empty()) {
            result.response = Quote("");
        }
        return result;
    }
    if (command.path == "SENS1:CORR:CSET:ACT" && command.query) {
        Count(command, 0U, 0U);
        auto result = Handled();
        result.response = Quote(std::to_string(snapshot.selectedSetId));
        return result;
    }
    if (!command.query && (command.path == "SENS1:CORR:CSET:ACT" || command.path == "SENS1:CORR:CSET:DEL")) {
        Count(command, 1U, 1U);
        VnaService::CalibrationChange change;
        change.kind = command.path == "SENS1:CORR:CSET:ACT" ? VnaService::CalibrationChangeKind::SELECT
                                                            : VnaService::CalibrationChangeKind::DELETE_SET;
        change.setId = Identifier(command.arguments[0]);
        auto result = Handled();
        result.operation = service_.ApplyCalibration(std::move(change));
        return result;
    }
    return {};
}

}  // 命名空间 VnaScpi
