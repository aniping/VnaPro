/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_query_policy.h"

#include <string>

#include "scpi/scpi_parser.h"
#include "vna_display/display_workspace.h"
#include "vna_display/marker_collection.h"

namespace VnaScpi {
namespace {

bool ValidIndex(const std::string &path, std::size_t begin, unsigned maximum, std::size_t &end) noexcept
{
    unsigned value = 0U;
    end = begin;
    while (end < path.size() && path[end] >= '0' && path[end] <= '9') {
        const auto digit = static_cast<unsigned>(path[end] - '0');
        if (value > maximum / 10U || value * 10U + digit > maximum) {
            return false;
        }
        value = value * 10U + digit;
        ++end;
    }
    return end > begin && value != 0U;
}

bool DisplayQuery(const std::string &path) noexcept
{
    const char prefix[] = "DISP:WIND";
    constexpr std::size_t prefixLength = sizeof(prefix) - 1U;
    if (path.compare(0U, prefixLength, prefix) != 0) {
        return false;
    }
    std::size_t separator = 0U;
    if (!ValidIndex(path, prefixLength, VnaDisplay::MAX_DISPLAY_DIAGRAMS, separator)) {
        return false;
    }
    if (separator == path.size()) {
        return true;
    }
    if (path[separator] != ':') {
        return false;
    }
    if (path.compare(separator + 1U, std::string::npos, "STAT") == 0 ||
        path.compare(separator + 1U, std::string::npos, "CAT") == 0 ||
        path.compare(separator + 1U, std::string::npos, "SIZE") == 0) {
        return true;
    }
    const char tracePrefix[] = ":TRAC";
    constexpr std::size_t tracePrefixLength = sizeof(tracePrefix) - 1U;
    if (path.compare(separator, tracePrefixLength, tracePrefix) != 0 ||
        !ValidIndex(path, separator + tracePrefixLength, VnaDisplay::MAX_DISPLAY_TRACES, separator)) {
        return false;
    }
    if (separator == path.size()) {
        return true;
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

bool MarkerQuery(const std::string &path) noexcept
{
    const char prefix[] = "CALC1:MARK";
    constexpr std::size_t prefixLength = sizeof(prefix) - 1U;
    std::size_t separator = 0U;
    if (path.compare(0U, prefixLength, prefix) != 0 ||
        !ValidIndex(path, prefixLength, VnaDisplay::MAX_MARKERS, separator)) {
        return false;
    }
    if (separator == path.size()) {
        return true;
    }
    if (path[separator] != ':') {
        return false;
    }
    const char * const leaves[] = {"STAT", "X", "Y", "DISC"};
    for (const auto *leaf : leaves) {
        if (path.compare(separator + 1U, std::string::npos, leaf) == 0) {
            return true;
        }
    }
    return false;
}

}  // 匿名命名空间

bool IsObserverQuery(const ParsedCommand &original)
{
    const auto command = RouteChannelCommand(original);
    if (!command.query || command.errorCode != 0) {
        return false;
    }
    const char * const paths[] = {"*IDN", "*OPC", "*ESE", "*SRE", "*STB", "SYST:ERR:COUN", "SENS1:FREQ:STAR",
        "SENS1:FREQ:STOP", "SENS1:SWE:POIN", "SENS1:BAND", "SENS1:BAND:RES", "SENS1:BWID", "SENS1:BWID:RES",
        "SENS1:SWE:TYPE", "TRIG:SOUR", "TRIG:SEQ:SOUR", "TRIG:SCOP", "TRIG:SEQ:SCOP", "INIT:CONT", "INIT1:CONT",
        "SENS1:SWE:MODE", "CALC1:PAR:SEL", "CALC1:PAR:CAT:EXT", "FORM", "FORM:DATA", "SENS1:X", "SENS1:X:VAL",
        "CALC1:DATA", "CALC1:FORM", "SENS1:CORR", "SENS1:CORR:STAT", "SENS1:CORR:COLL:STAT", "SENS1:CORR:CSET:CAT",
        "SENS1:CORR:CSET:ACT", "SOUR1:POW", "SOUR1:POW:LEV", "SOUR1:POW:IMM", "SOUR1:POW:AMPL", "SOUR1:POW:LEV:IMM",
        "SOUR1:POW:LEV:AMPL", "SOUR1:POW:IMM:AMPL", "SOUR1:POW:LEV:IMM:AMPL"};
    for (const auto *path : paths) {
        if (command.path == path) {
            return true;
        }
    }
    return DisplayQuery(command.path) || MarkerQuery(command.path);
}

}  // 命名空间 VnaScpi
