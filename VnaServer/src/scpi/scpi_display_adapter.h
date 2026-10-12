/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_DISPLAY_ADAPTER_H
#define VNAPRO_SCPI_DISPLAY_ADAPTER_H

#include <string>

#include "scpi/parsed_command.h"
#include "vna_display/display_workspace.h"
#include "vna_service/instrument_service.h"

namespace VnaScpi {

// 协议转换结果交回连接引擎，统一维护错误队列、响应边界和 OPC 屏障。
struct ScpiDisplayResult final {
    bool matched = false;
    int errorCode = 0;
    std::string error;
    std::string response;
    VnaService::Operation operation;
};

// 仅承担标准命令映射；显示状态、格式与 Marker 计算由仪器后端维护。
class ScpiDisplayAdapter final {
public:
    explicit ScpiDisplayAdapter(VnaService::InstrumentService &service);
    // 此调用发生于连接 FIFO 执行时，因此未指定名称的操作使用执行时选择。
    ScpiDisplayResult Execute(const ParsedCommand &command) const;

private:
    ScpiDisplayResult Selection(const ParsedCommand &command) const;
    ScpiDisplayResult Format(const ParsedCommand &command) const;
    ScpiDisplayResult Marker(const ParsedCommand &command, unsigned id, const std::string &leaf) const;
    ScpiDisplayResult AllMarkersOff(const ParsedCommand &command) const;
    ScpiDisplayResult Window(const ParsedCommand &command, unsigned diagram, const std::string &leaf) const;
    ScpiDisplayResult Trace(
        const ParsedCommand &command, unsigned diagram, unsigned slot, const std::string &leaf) const;
    ScpiDisplayResult ReadMarker(
        const VnaDisplay::TracePresentation &trace, unsigned id, const std::string &leaf) const;
    ScpiDisplayResult SetMarkerFrequency(const ParsedCommand &command, VnaDisplay::DisplayChange change) const;
    ScpiDisplayResult ReadTrace(const VnaDisplay::TracePresentation &trace, const std::string &leaf) const;
    ScpiDisplayResult SetTrace(const ParsedCommand &command, const VnaDisplay::TracePresentation &trace,
        VnaDisplay::DisplayChange change, const std::string &leaf) const;
    ScpiDisplayResult SetScale(const ParsedCommand &command, const VnaDisplay::TracePresentation &trace,
        VnaDisplay::DisplayChange change, const std::string &leaf) const;

    VnaService::InstrumentService &service_;
};

}  // 命名空间 VnaScpi
#endif
