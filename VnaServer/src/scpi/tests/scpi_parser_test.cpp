/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_parser.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void ExpectPath(const std::string &input, const std::string &expected)
{
    std::vector<std::string> parent;
    const auto parsed = VnaScpi::ParseCommand(input, parent);
    if (parsed.errorCode != 0 || parsed.path != expected) {
        throw std::runtime_error("命令头规范化不符：" + input + " -> " + parsed.path);
    }
}

void DisplayKeywordsAndDefaults()
{
    ExpectPath("DISPlay:WINDow:TRACe:Y:SCALe:PDIVision?", "DISP:WIND1:TRAC1:Y:SCAL:PDIV");
    ExpectPath("DISP:WIND1:TRAC4:Y:RLEV 3", "DISP:WIND1:TRAC4:Y:RLEV");
    ExpectPath("DISPlay:WINDow2:TRACe3:Y:SCALe:RPOSition?", "DISP:WIND2:TRAC3:Y:SCAL:RPOS");
    ExpectPath("DISPlay:WINDow:TRACe:STATe ON", "DISP:WIND1:TRAC1:STAT");
    ExpectPath("DISPlay:WINDow:TRACe:SELect", "DISP:WIND1:TRAC1:SEL");
    ExpectPath("DISPlay:WINDow:TRACe:Y:AUTO", "DISP:WIND1:TRAC1:Y:AUTO");
}

void MarkerKeywordsAndDefaults()
{
    ExpectPath("CALCulate:MARKer:STATe ON", "CALC1:MARK1:STAT");
    ExpectPath("CALC:MARK1:X?", "CALC1:MARK1:X");
    ExpectPath("CALCulate2:MARKer3:Y?", "CALC2:MARK3:Y");
    ExpectPath("CALCulate:MARKer:DISCrete ON", "CALC1:MARK1:DISC");
    ExpectPath("CALCulate:MARKer10:STATe ON", "CALC1:MARK10:STAT");
    ExpectPath("CALCulate:MARKer:AOFF", "CALC1:MARK:AOFF");
    ExpectPath("CALCulate:MARKer1:AOFF", "CALC1:MARK1:AOFF");
    ExpectPath("CALCulate:FORMat MLOGarithmic", "CALC1:FORM");
}

void RelativeDisplayPaths()
{
    std::vector<std::string> parent;
    const auto scale = VnaScpi::ParseCommand("DISPlay:WINDow1:TRACe2:Y:SCALe:PDIVision 2", parent);
    const auto level = VnaScpi::ParseCommand("RLEVel 5", parent);
    const auto position = VnaScpi::ParseCommand("RPOSition?", parent);
    const auto marker = VnaScpi::ParseCommand(":CALCulate:MARKer:X 1GHz", parent);
    const auto value = VnaScpi::ParseCommand("Y?", parent);
    if (scale.path != "DISP:WIND1:TRAC2:Y:SCAL:PDIV" || level.path != "DISP:WIND1:TRAC2:Y:SCAL:RLEV" ||
        position.path != "DISP:WIND1:TRAC2:Y:SCAL:RPOS" || !position.query || marker.path != "CALC1:MARK1:X" ||
        value.path != "CALC1:MARK1:Y" || !value.query) {
        throw std::runtime_error("显示和 Marker 相对路径未保持父节点");
    }
}

void OmittedMarkerStateHasItsWrittenParent()
{
    std::vector<std::string> parent;
    const auto enabled = VnaScpi::ParseCommand("CALC:MARK2 ON", parent);
    const auto relative = VnaScpi::ParseCommand("X MAX", parent);
    if (enabled.path != "CALC1:MARK2" || relative.path != "CALC1:X") {
        throw std::runtime_error("相对路径应使用实际写出的父节点，不能凭空补入省略的 STAT");
    }
    const auto explicitState = VnaScpi::ParseCommand(":CALC:MARK2:STAT ON", parent);
    const auto markerPosition = VnaScpi::ParseCommand("X MAX", parent);
    if (explicitState.path != "CALC1:MARK2:STAT" || markerPosition.path != "CALC1:MARK2:X") {
        throw std::runtime_error("显式 STAT 后的 X 应保持在对应标记下");
    }
}

}  // 匿名命名空间

int main()
{
    try {
        DisplayKeywordsAndDefaults();
        MarkerKeywordsAndDefaults();
        RelativeDisplayPaths();
        OmittedMarkerStateHasItsWrittenParent();
        std::cout << "PASS 显示与 Marker 命令头规范化\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
