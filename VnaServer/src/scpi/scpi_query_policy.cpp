/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_query_policy.h"

#include <string>

#include "scpi/scpi_command_registry.h"
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
    if (const auto *definition = FindCommand(command.path)) {
        return definition->query && definition->observer;
    }
    const char * const paths[] = {"CALC1:PAR:SEL", "CALC1:FORM", "SENS1:CORR", "SENS1:CORR:STAT",
        "SENS1:CORR:COLL:STAT", "SENS1:CORR:CSET:CAT", "SENS1:CORR:CSET:ACT"};
    for (const auto *path : paths) {
        if (command.path == path) {
            return true;
        }
    }
    return DisplayQuery(command.path) || MarkerQuery(command.path);
}

}  // 命名空间 VnaScpi
