/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_command_registry.h"

namespace VnaScpi {
namespace {
using Handler = CommandHandler;
constexpr CommandDefinition g_commands[] = {
    {"*IDN", Handler::IDENTITY, false, true, true},
    {"*CLS", Handler::CLEAR_STATUS, true, false, false},
    {"*RST", Handler::RESET, true, false, false},
    {"*OPC", Handler::OPERATION_COMPLETE, true, true, true},
    {"*WAI", Handler::WAIT, true, false, false},
    {"*ESE", Handler::ENABLE_REGISTER, true, true, true},
    {"*SRE", Handler::ENABLE_REGISTER, true, true, true},
    {"*ESR", Handler::STATUS_REGISTER, false, true, false},
    {"*STB", Handler::STATUS_REGISTER, false, true, true},
    {"SYST:ERR", Handler::NEXT_ERROR, false, true, false},
    {"SYST:ERR:NEXT", Handler::NEXT_ERROR, false, true, false},
    {"SYST:ERR:COUN", Handler::ERROR_COUNT, false, true, true},
    {"SENS1:FREQ:STAR", Handler::FREQUENCY, true, true, true},
    {"SENS1:FREQ:STOP", Handler::FREQUENCY, true, true, true},
    {"SENS1:SWE:POIN", Handler::POINTS, true, true, true},
    {"SENS1:BAND", Handler::BANDWIDTH, true, true, true},
    {"SENS1:BAND:RES", Handler::BANDWIDTH, true, true, true},
    {"SENS1:BWID", Handler::BANDWIDTH, true, true, true},
    {"SENS1:BWID:RES", Handler::BANDWIDTH, true, true, true},
    {"SENS1:SWE:TYPE", Handler::SWEEP_TYPE, true, true, true},
    {"SENS1:SWE:TIME", Handler::SWEEP_TIMING, true, true, true},
    {"SENS1:SWE:TIME:AUTO", Handler::SWEEP_TIMING, true, true, true},
    {"SOUR1:POW", Handler::POWER, true, true, true},
    {"SOUR1:POW:LEV", Handler::POWER, true, true, true},
    {"SOUR1:POW:IMM", Handler::POWER, true, true, true},
    {"SOUR1:POW:AMPL", Handler::POWER, true, true, true},
    {"SOUR1:POW:LEV:IMM", Handler::POWER, true, true, true},
    {"SOUR1:POW:LEV:AMPL", Handler::POWER, true, true, true},
    {"SOUR1:POW:IMM:AMPL", Handler::POWER, true, true, true},
    {"SOUR1:POW:LEV:IMM:AMPL", Handler::POWER, true, true, true},
    {"TRIG:SOUR", Handler::TRIGGER_SOURCE, true, true, true},
    {"TRIG:SEQ:SOUR", Handler::TRIGGER_SOURCE, true, true, true},
    {"INIT:CONT", Handler::TRIGGER_SOURCE, true, true, true},
    {"INIT1:CONT", Handler::TRIGGER_SOURCE, true, true, true},
    {"TRIG:SCOP", Handler::TRIGGER_SCOPE, true, true, true},
    {"TRIG:SEQ:SCOP", Handler::TRIGGER_SCOPE, true, true, true},
    {"SENS1:SWE:MODE", Handler::SWEEP_MODE, true, true, true},
    {"INIT", Handler::INITIATE, true, false, false},
    {"INIT1", Handler::INITIATE, true, false, false},
    {"INIT:IMM", Handler::INITIATE, true, false, false},
    {"INIT1:IMM", Handler::INITIATE, true, false, false},
    {"CALC1:PAR:DEF:EXT", Handler::DEFINE_MEASUREMENT, true, false, false},
    {"CALC1:PAR:EXT", Handler::DEFINE_MEASUREMENT, true, false, false},
    {"CALC1:PAR:CAT:EXT", Handler::MEASUREMENT_CATALOG, false, true, true},
    {"CALC1:PAR:DEL", Handler::DELETE_MEASUREMENT, true, false, false},
    {"CALC1:PAR:DEL:NAME", Handler::DELETE_MEASUREMENT, true, false, false},
    {"CALC1:PAR:DEL:ALL", Handler::DELETE_ALL_MEASUREMENTS, true, false, false},
    {"FORM", Handler::DATA_FORMAT, true, true, true},
    {"FORM:DATA", Handler::DATA_FORMAT, true, true, true},
    {"FORM:BORD", Handler::BYTE_ORDER, true, true, true},
    {"SENS1:X", Handler::STIMULUS_AXIS, false, true, true},
    {"SENS1:X:VAL", Handler::STIMULUS_AXIS, false, true, true},
    {"CALC1:DATA", Handler::MEASUREMENT_DATA, false, true, true},
    {"CALC1:DATA:MSD", Handler::MULTIPLE_MEASUREMENT_DATA, false, true, true},
};
}  // 匿名命名空间

const CommandDefinition *FindCommand(const std::string &path) noexcept
{
    for (const auto &command : g_commands) {
        if (path == command.path) {
            return &command;
        }
    }
    return nullptr;
}
}  // 命名空间 VnaScpi
