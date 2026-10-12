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

}  // 匿名命名空间

int main()
{
    try {
        DisplayKeywordsAndDefaults();
        MarkerKeywordsAndDefaults();
        RelativeDisplayPaths();
        std::cout << "PASS 显示与 Marker 命令头规范化\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
