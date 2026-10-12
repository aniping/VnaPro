/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../../../../VnaNativeSimulator/tests/scoped_simulator_environment.h"
#include "native_fixture.h"
#include "scpi/scpi_engine.h"
#include "vna_display/display_workspace.h"

namespace {

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Near(double value, double expected)
{
    const double tolerance = 1e-10 * (std::max)(1.0, std::fabs(expected));
    Expect(std::fabs(value - expected) <= tolerance, "数值不符：" + std::to_string(value));
}

void Drain(VnaScpi::ScpiEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (engine.HasPending()) {
        engine.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "SCPI 命令队列未结束");
        std::this_thread::yield();
    }
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &text)
{
    Expect(engine.SubmitLine(text), "SCPI 输入被拒绝");
    Drain(engine);
    return engine.TakeOutput();
}

void Await(VnaService::InstrumentService &service, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "仪器操作未结束");
        std::this_thread::yield();
    }
    Expect(operation.get().ok, operation.get().message);
}

std::vector<double> Numbers(const std::string &text)
{
    std::vector<double> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        values.push_back(std::stod(item));
    }
    return values;
}

std::string Number(double value)
{
    std::ostringstream stream;
    stream.precision(17);
    stream << value;
    return stream.str();
}

const VnaDisplay::TracePresentation &Trace(const VnaService::InstrumentService &service, const std::string &name)
{
    for (const auto &trace : service.Display().traces) {
        if (trace.name == name) {
            return trace;
        }
    }
    throw std::runtime_error("显示迹线不存在：" + name);
}

void SelectionFormatAndFifo(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    const auto response = Command(engine,
        "CALC:PAR:DEF:EXT 'First','S11';:CALC:PAR:DEF:EXT 'Second','S11';"
        ":CALC:PAR:SEL 'First';:CALC:FORM REAL;:CALC:PAR:SEL 'Second';:CALC:FORM PHASe;"
        ":CALC:PAR:SEL?;:CALC:FORM?");
    Expect(response == "\"Second\";PHAS\n", "顺序执行未采用执行时的活动测量");
    Expect(Trace(fixture.service, "First").format == VnaDisplay::DisplayFormat::REAL &&
               Trace(fixture.service, "Second").format == VnaDisplay::DisplayFormat::PHASE,
        "SCPI 格式未提交到后端的各迹线状态");
    const std::vector<std::pair<std::string, std::string>> formats = {{"MLOGarithmic", "MLOG"}, {"MLINear", "MLIN"},
        {"PHASe", "PHAS"}, {"REAL", "REAL"}, {"IMAGinary", "IMAG"}, {"SMITh", "SMIT"}, {"SWR", "SWR"},
        {"UPHase", "UPH"}, {"GDELay", "GDEL"}, {"POLar", "POL"}};
    for (const auto &format : formats) {
        Expect(Command(engine, "CALC:FORM " + format.first + ";FORM?") == format.second + "\n",
            "正式长格式未读回标准短格式");
    }
    Expect(Command(engine, "SYST:ERR?").find("0,") == 0U, "合法格式产生错误");
}

void DisplaySlotsAndVisibility(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "CALC:PAR:DEF:EXT 'Second','S11'");
    VnaDisplay::DisplayChange displayed;
    displayed.kind = VnaDisplay::DisplayChangeKind::DISPLAYED;
    displayed.hasDisplayedNames = true;
    displayed.displayedNames = {"Second", "Trc1"};
    Await(fixture.service, fixture.service.ApplyDisplay(displayed));
    const auto response = Command(engine, "DISPlay:WINDow:TRACe:SELect;:CALC:PAR:SEL?");
    Expect(response == "\"Second\"\n", "显示槽号错误地使用测量目录下标：" + response);
    Command(engine, "DISP:WIND:TRAC:STAT OFF");
    Expect(!Trace(fixture.service, "Second").visible && Trace(fixture.service, "Trc1").visible,
        "显示槽可见性写入了错误的测量");
    Expect(Command(engine, "DISP:WIND:TRAC:STAT?") == "0\n", "显示槽可见性查询未读取共享状态");
    Command(engine, "DISP:WIND:TRAC ON");
    Expect(Command(engine, "DISP:WIND:TRAC:STAT?") == "1\n", "省略 STAT 的正式短式未生效");
}

void ScaleAndSmithContracts(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    const auto physical = fixture.core.ReadView();
    auto response = Command(engine,
        "DISPlay:WINDow:TRACe:Y:SCALe:PDIVision 2;RLEVel -5;RPOSition 3;"
        "PDIVision?;RLEVel?;RPOSition?");
    std::replace(response.begin(), response.end(), ';', ',');
    const auto readback = Numbers(response);
    Expect(readback.size() == 3U, "Scale 查询的复合响应不符");
    Near(readback[0], 2.0);
    Near(readback[1], -5.0);
    Near(readback[2], 3.0);
    const auto &scale = Trace(fixture.service, "Trc1").scale;
    Near(scale.referencePosition, 3.0);
    Near(scale.min, -11.0);
    Near(scale.max, 9.0);
    const auto unchanged = fixture.core.ReadView();
    Expect(unchanged.configRevision == physical.configRevision && unchanged.runId == physical.runId &&
               unchanged.frame == physical.frame,
        "仅显示尺度变化触碰了物理采集状态");
    Command(engine, "CALC:PAR:DEF:EXT 'Reflection','S11';:CALC:PAR:SEL 'Reflection';:CALC:FORM SMIT");
    VnaDisplay::DisplayChange displayed;
    displayed.kind = VnaDisplay::DisplayChangeKind::DISPLAYED;
    displayed.hasDisplayedNames = true;
    displayed.displayedNames = {"Reflection"};
    Await(fixture.service, fixture.service.ApplyDisplay(displayed));
    Command(engine, "DISP:WIND:TRAC:Y:PDIV 2.5");
    Near(std::stod(Command(engine, "DISP:WIND:TRAC:Y:PDIV?")), 2.5);
    Near(Trace(fixture.service, "Reflection").smithRadius, 2.5);
    Expect(Command(engine, "DISP:WIND:TRAC:Y:RLEV?").empty(), "Smith 不应伪造参考值");
    Expect(Command(engine, "SYST:ERR?").find("-221,") == 0U, "Smith 不适用设置应记录状态冲突");
}

void MarkerInterpolatesAndUsesFullFrame(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "SENS:SWE:POIN 10001;:CALC:PAR:DEF:EXT 'Raw','a1,1';:CALC:PAR:SEL 'Raw';:CALC:FORM REAL");
    Command(engine, "SENS:SWE:MODE SING;*OPC?");
    const auto held = fixture.core.ReadView().frame;
    Expect(held && held->plan.frequencyHz.size() == 10001U, "完整帧点数不符");
    const auto values = fixture.service.MeasurementData();
    Expect(values.ok && values.values.size() == 20002U, "完整复数波量不可用");
    const double frequency = held->plan.frequencyHz[9171U] * 0.75 + held->plan.frequencyHz[9172U] * 0.25;
    Command(engine, "CALC:MARK:STAT ON;X " + Number(frequency) + ";:CALC:MARK:DISC OFF");
    const auto interpolated = Numbers(Command(engine, "CALC:MARK:Y?"));
    Expect(interpolated.size() == 2U, "Marker 标量响应应始终含两个数值");
    Near(interpolated[0], values.values[9171U * 2U] * 0.75 + values.values[9172U * 2U] * 0.25);
    Near(interpolated[1], 0.0);
    Near(std::stod(Command(engine, "CALC:MARK:X?")), frequency);
    Command(engine, "CALC:MARK:DISC ON");
    Expect(Command(engine, "CALC:MARK:DISC?") == "1\n", "离散 Marker 状态未共享");
    const auto discrete = Numbers(Command(engine, "CALC:MARK:Y?"));
    Near(discrete.at(0), values.values[9171U * 2U]);
    Near(std::stod(Command(engine, "CALC:MARK:X?")), held->plan.frequencyHz[9171U]);
    Expect(fixture.core.ReadView().frame == held, "Marker 查询或状态变化修改了原始帧");
}

void MarkerErrorsAndSmithPairs(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;CALC:MARK:STAT ON");
    Expect(Command(engine, "CALC:MARK:Y?").empty(), "没有采集帧不应返回假 Marker 数值");
    Expect(Command(engine, "SYST:ERR?").find("-230,") == 0U, "未就绪 Marker 读取需数据错误");
    Command(engine, "CALC:PAR:DEF:EXT 'Reflection','S11';:CALC:PAR:SEL 'Reflection';:CALC:FORM SMIT");
    Command(engine, "SENS:SWE:MODE SING;*OPC?;:CALC:MARK:STAT ON;:CALC:MARK:X MIN");
    const auto raw = fixture.service.MeasurementData();
    const auto marker = Numbers(Command(engine, "CALC:MARK:Y?"));
    Expect(raw.ok && marker.size() == 2U, "Smith Marker 响应缺少复数对");
    Near(marker[0], raw.values[0]);
    Near(marker[1], raw.values[1]);
    Command(engine, "CALC:FORM POLar");
    const auto polar = Numbers(Command(engine, "CALC:MARK:Y?"));
    Expect(polar.size() == 2U, "Polar 标记应返回标准复数对");
    Near(polar[0], raw.values[0]);
    Near(polar[1], raw.values[1]);
    Command(engine, "CALC:MARK:STAT OFF");
    Expect(Command(engine, "CALC:MARK:Y?").empty(), "关闭 Marker 后不应生成读数");
    Expect(Command(engine, "SYST:ERR?").find("-221,") == 0U, "关闭 Marker 应记录状态冲突");
}

void NumberedMarkersAreIndependentAndAllOffIsScoped(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "CALC:FORM REAL;:SENS:SWE:MODE SING;*OPC?");
    const auto frame = fixture.core.ReadView().frame;
    const auto raw = fixture.service.MeasurementData();
    Expect(frame && raw.ok, "多标记测试需要完整帧");
    Command(engine, "CALC:MARK1:STAT ON;X MIN;DISC ON;:CALC:MARK2:STAT ON;X MAX;DISC ON");
    Expect(Command(engine, "CALC:MARK1:STAT?;:CALC:MARK2:STAT?") == "1;1\n", "M1/M2 未独立启用");
    Near(std::stod(Command(engine, "CALC:MARK1:X?")), frame->plan.frequencyHz.front());
    Near(std::stod(Command(engine, "CALC:MARK2:X?")), frame->plan.frequencyHz.back());
    Near(Numbers(Command(engine, "CALC:MARK1:Y?")).at(0), raw.values.front());
    Near(Numbers(Command(engine, "CALC:MARK2:Y?")).at(0), raw.values[raw.values.size() - 2U]);
    Expect(Trace(fixture.service, "Trc1").markers.Selected() == 2U, "显式 M1 查询不应改动当前活动 M2");
    Command(engine, "CALCulate:MARKer10:STATe ON;X 25MHz;DISCrete OFF");
    Expect(Command(engine, "CALC:MARK10:STAT?;DISC?") == "1;0\n", "M10 边界编号或长命令未正确处理");
    Expect(Command(engine, "CALC:MARK1:DISC?;:CALC:MARK2:DISC?;:CALC:MARK10:DISC?") == "1;1;0\n",
        "改变 M10 的插值不能覆盖 M1/M2 的离散设置");
    Near(std::stod(Command(engine, "CALC:MARK10:X?")), 25000000.0);
    for (unsigned id = 1U; id <= 10U; ++id) {
        const auto expected = id == 1U || id == 2U || id == 10U ? "1\n" : "0\n";
        Expect(Command(engine, "CALC:MARK" + std::to_string(id) + ":STAT?") == expected, "不同编号的状态相互污染");
    }
    Command(engine, "CALC:MARK2:STAT OFF");
    Expect(Command(engine, "CALC:MARK1:STAT?;:CALC:MARK2:STAT?;:CALC:MARK10:STAT?") == "1;0;1\n",
        "关闭 M2 不应关闭 M1 或 M10");
    Command(engine, "CALC:MARK2 ON");
    Near(std::stod(Command(engine, "CALC:MARK2:X?")), frame->plan.frequencyHz.back());
    Command(engine,
        "CALC:PAR:DEF:EXT 'Other','S21';:CALC:PAR:SEL 'Other';:CALC:MARK3:STAT ON;"
        ":CALC:PAR:SEL 'Trc1';:CALC:MARK:AOFF");
    for (unsigned id = 1U; id <= 10U; ++id) {
        Expect(
            Command(engine, "CALC:MARK" + std::to_string(id) + ":STAT?") == "0\n", "AOFF 未关闭当前迹线全部普通标记");
    }
    Expect(Command(engine, "CALC:PAR:SEL 'Other';:CALC:MARK3:STAT?") == "1\n", "AOFF 错误地修改了另一迹线标记");
    Command(engine, "CALC:PAR:SEL 'Trc1';:CALC:MARK2:STAT ON");
    Near(std::stod(Command(engine, "CALC:MARK2:X?")), frame->plan.frequencyHz.back());
    Expect(Command(engine, "SYST:ERR?").find("0,") == 0U, "合法多标记操作产生错误");
    Expect(fixture.core.ReadView().frame == frame, "多标记操作不应修改完整采集帧");
}

void NumberedMarkerHeadersRejectUnsupportedActions(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    const auto revision = fixture.service.Display().revision;
    for (const auto &command : {"CALC:MARK0:STAT ON", "CALC:MARK11:STAT ON", "CALC:MARK42949672960:STAT ON",
             "CALC:MARK2:Y 1", "CALC:MARK2:AOFF", "CALC:MARK1:AOFF", "CALC:MARK:AOFF?", "CALC:MARK:SEL 2"}) {
        Command(engine, command);
        Expect(Command(engine, "SYST:ERR?").find("-113,") == 0U, "超范围或不支持的标记命令头应明确拒绝");
    }
    Command(engine, "CALC:MARK:AOFF ON");
    Expect(Command(engine, "SYST:ERR?").find("-108,") == 0U, "AOFF 不应接受额外参数");
    Expect(fixture.service.Display().revision == revision, "非法多标记操作修改了共享状态");
}

void DisplaySlotsReachConfiguredTraceCapacity(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    VnaDisplay::DisplayChange displayed;
    displayed.kind = VnaDisplay::DisplayChangeKind::DISPLAYED;
    displayed.hasDisplayedNames = true;
    displayed.displayedNames.push_back("Trc1");
    for (unsigned id = 2U; id <= VnaDisplay::MAX_DISPLAY_TRACES; ++id) {
        const auto name = "Trc" + std::to_string(id);
        Command(engine, "CALC:PAR:DEF:EXT '" + name + "','S21'");
        displayed.displayedNames.push_back(name);
    }
    Await(fixture.service, fixture.service.ApplyDisplay(displayed));
    const auto lastSlot = std::to_string(VnaDisplay::MAX_DISPLAY_TRACES);
    Expect(Command(engine, "DISP:WIND:TRAC" + lastSlot + ":SEL;:CALC:PAR:SEL?") == "\"Trc" + lastSlot + "\"\n",
        "超过四条迹线后的显示槽未映射到后端迹线");
    Command(engine, "DISP:WIND:TRAC" + lastSlot + ":STAT OFF");
    Expect(Command(engine, "DISP:WIND:TRAC" + lastSlot + ":STAT?") == "0\n", "末尾显示槽不能独立读写");
    Expect(Trace(fixture.service, "Trc1").visible, "末尾显示槽修改污染了第一条迹线");
    Command(engine, "DISP:WIND:TRAC" + std::to_string(VnaDisplay::MAX_DISPLAY_TRACES + 1U) + ":STAT ON");
    Expect(Command(engine, "SYST:ERR?").find("-222,") == 0U, "超出资源容量的显示槽应拒绝");
}

void UnsupportedFormatsAndActions(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    for (const auto &format :
        {"REALIMAG", "COMP", "SLOG", "SCOM", "LOGMagnitude", "DelayDerivation", "InvSmith", "SWRat"}) {
        Command(engine, std::string("CALC:FORM ") + format);
        Expect(Command(engine, "SYST:ERR?").find("-224,") == 0U, "不支持的格式不能伪装兼容");
    }
    Command(engine, "CALC:PAR:SEL 'Trc1',FAST");
    Expect(Command(engine, "SYST:ERR?").find("-108,") == 0U, "未实现 FAST 不应接受");
    const auto revision = fixture.service.Display().revision;
    for (const auto &command : {"CALC:MARK11:STAT ON", "DISP:WIND:TRAC:Y:AUTO?"}) {
        Command(engine, command);
        Expect(Command(engine, "SYST:ERR?").find("-113,") == 0U, "不支持的 Marker 或窗口应拒绝命令头");
    }
    Command(engine, "DISP:WIND2:TRAC1:STAT ON");
    Expect(Command(engine, "SYST:ERR?").find("-222,") == 0U, "未创建图窗的标准路径应拒绝状态而非命令头");
    Expect(fixture.service.Display().revision == revision, "不支持的操作修改了共享状态");
}

void AutoscaleIsOneActionDuringContinuous(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "SENS:SWE:MODE SING;*OPC?;:SENS:SWE:MODE CONT;*OPC?");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!fixture.service.DisplayData().available) {
        engine.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "CONT 首个完整显示帧未发布");
        std::this_thread::yield();
    }
    const auto before = fixture.core.ReadView();
    Command(engine, "DISP:WIND:TRAC:Y:AUTO;*OPC?");
    Expect(Command(engine, "SYST:ERR?").find("0,") == 0U, "当前完整帧的 Autoscale 未成功");
    const auto scale = Trace(fixture.service, "Trc1").scale;
    const auto revision = fixture.service.Display().revision;
    Command(engine, "*OPC?");
    const auto after = fixture.core.ReadView();
    const auto &retained = Trace(fixture.service, "Trc1").scale;
    Near(retained.min, scale.min);
    Near(retained.max, scale.max);
    Expect(after.runId == before.runId && after.configRevision == before.configRevision &&
               fixture.service.Display().revision == revision &&
               fixture.service.Settings().sweepMode == VnaService::SweepMode::CONTINUOUS,
        "Autoscale 不应重启采集或按连续帧更新共享尺度");
    Command(engine, "SENS:SWE:MODE HOLD");
}

}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    VnaTest::ScopedSimulatorEnvironment environment("golden", nullptr);
    try {
        SelectionFormatAndFifo(argv[1]);
        DisplaySlotsAndVisibility(argv[1]);
        ScaleAndSmithContracts(argv[1]);
        MarkerInterpolatesAndUsesFullFrame(argv[1]);
        MarkerErrorsAndSmithPairs(argv[1]);
        NumberedMarkersAreIndependentAndAllOffIsScoped(argv[1]);
        NumberedMarkerHeadersRejectUnsupportedActions(argv[1]);
        DisplaySlotsReachConfiguredTraceCapacity(argv[1]);
        UnsupportedFormatsAndActions(argv[1]);
        AutoscaleIsOneActionDuringContinuous(argv[1]);
        std::cout << "PASS SCPI 与共享显示状态合同\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
