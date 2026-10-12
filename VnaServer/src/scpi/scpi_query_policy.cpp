/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_query_policy.h"

#include <string>

namespace VnaScpi {
namespace {

bool DisplayQuery(const std::string &path) noexcept
{
    const char prefix[] = "DISP:WIND1:TRAC";
    constexpr std::size_t prefixLength = sizeof(prefix) - 1U;
    if (path.compare(0U, prefixLength, prefix) != 0) {
        return false;
    }
    const auto separator = path.find_first_not_of("0123456789", prefixLength);
    if (separator == prefixLength) {
        return false;
    }
    if (separator == std::string::npos) {
        return path.size() > prefixLength;
    }
    if (path[separator] != ':') {
        return false;
    }
    const char * const leaves[] = {"STAT", "Y:PDIV", "Y:RLEV", "Y:RPOS", "Y:SCAL:PDIV", "Y:SCAL:RLEV", "Y:SCAL:RPOS"};
    for (const auto *leaf : leaves) {
        if (path.compare(separator + 1U, std::string::npos, leaf) == 0) {
            return true;
        }
    }
    return false;
}

}  // 匿名命名空间

bool IsObserverQuery(const ParsedCommand &command) noexcept
{
    if (!command.query || command.errorCode != 0) {
        return false;
    }
    const char * const paths[] = {"*IDN", "*OPC", "*ESE", "*SRE", "*STB", "SYST:ERR:COUN", "SENS1:FREQ:STAR",
        "SENS1:FREQ:STOP", "SENS1:SWE:POIN", "SENS1:BAND", "SENS1:BAND:RES", "SENS1:BWID", "SENS1:BWID:RES",
        "SENS1:SWE:TYPE", "TRIG:SOUR", "TRIG:SEQ:SOUR", "TRIG:SCOP", "TRIG:SEQ:SCOP", "INIT:CONT", "INIT1:CONT",
        "SENS1:SWE:MODE", "CALC1:PAR:SEL", "CALC1:PAR:CAT:EXT", "FORM", "FORM:DATA", "SENS1:X", "SENS1:X:VAL",
        "CALC1:DATA", "CALC1:FORM", "CALC1:MARK1", "CALC1:MARK1:STAT", "CALC1:MARK1:X", "CALC1:MARK1:Y",
        "CALC1:MARK1:DISC"};
    for (const auto *path : paths) {
        if (command.path == path) {
            return true;
        }
    }
    return DisplayQuery(command.path);
}

}  // 命名空间 VnaScpi
