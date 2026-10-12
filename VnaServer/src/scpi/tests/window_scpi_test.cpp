/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../../../../VnaNativeSimulator/tests/scoped_simulator_environment.h"
#include "native_fixture.h"
#include "scpi/scpi_engine.h"

namespace {

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &line)
{
    Expect(engine.SubmitLine(line), "合法图窗命令未受理：" + line);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (engine.HasPending()) {
        engine.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "图窗命令未结束");
        std::this_thread::yield();
    }
    return engine.TakeOutput();
}

void Await(VnaService::InstrumentService &service, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "共享图窗事务未结束");
        std::this_thread::yield();
    }
    Expect(operation.get().ok, operation.get().message);
}

void SelectionDoesNotFeedHiddenMeasurement(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "CALC:PAR:DEF:EXT 'Hidden','S11';:CALC:PAR:SEL 'Hidden';:CALC:FORM REAL");
    Expect(fixture.service.Display().displayedNames == std::vector<std::string>{"Trc1"},
        "CALC:PAR:SEL 只能选择已定义测量，不能隐式加入图窗");
    Expect(Command(engine, "CALC:PAR:SEL?;:CALC:FORM?") == "\"Hidden\";REAL\n", "隐藏测量仍应支持选择和格式设置");
    Command(engine, "DISP:WIND:TRAC2:FEED 'Hidden'");
    Expect(Command(engine, "DISP:WIND:CAT?") == "\"1,2\"\n", "FEED 未显式添加第二个局部槽");
}

void WindowLocalSlotsAndViewDeletion(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "CALC:PAR:DEF:EXT 'Second','S11';:DISPlay:WINDow2:STATe ON");
    Expect(Command(engine, "DISP:WIND2:STAT?;CAT?;:DISP:WIND1:CAT?") == "1;\"EMPTY\";\"1\"\n",
        "窗口目录应返回局部槽号并保留空图窗");
    const auto revision = fixture.service.Display().revision;
    Command(engine, "DISP:WIND2 ON");
    Expect(fixture.service.Display().revision == revision, "重复打开已存在窗口应为幂等操作");
    Command(engine,
        "DISP:WIND2:TRAC1:FEED 'Second';Y:SCAL:PDIV 2;:DISP:WIND1:TRAC1:Y:PDIV 5;"
        ":DISP:WIND2:TRAC1:SEL;:CALC:PAR:SEL?");
    Expect(Command(engine, "CALC:PAR:SEL?") == "\"Second\"\n", "窗口二第一槽没有选择自身迹线");
    Expect(std::stod(Command(engine, "DISP:WIND2:TRAC1:Y:PDIV?")) == 2.0 &&
               std::stod(Command(engine, "DISP:WIND1:TRAC1:Y:PDIV?")) == 5.0,
        "两个图窗的 TRAC1 尺度相互污染");
    Command(engine, "DISP:WIND2:TRAC1:STAT OFF");
    Expect(Command(engine, "DISP:WIND2:TRAC1:STAT?;:DISP:WIND1:TRAC1:STAT?") == "0;1\n", "窗口局部槽的可见性读写错误");
    Command(engine, "DISP:WIND2:SIZE MAX");
    Expect(Command(engine, "DISP:WIND2:SIZE?;:DISP:WIND1:SIZE?") == "MAX;NORM\n" &&
               fixture.service.Display().selectedDiagram == 2U,
        "最大化必须原子选中指定图窗，保留其他图窗");
    Command(engine, "DISP:WIND2:SIZE NORM");
    Expect(Command(engine, "DISP:WIND2:SIZE?") == "NORM\n", "恢复普通布局失败");
    Command(engine, "DISP:WIND2:TRAC1:DEL");
    Expect(Command(engine, "DISP:WIND2:CAT?") == "\"EMPTY\"\n", "DEL 应只移除局部显示槽");
    Expect(
        Command(engine, "CALC:PAR:CAT:EXT?").find("Second,S11") != std::string::npos, "显示槽 DEL 错误删除了测量定义");
    Command(engine, "DISP:WIND2 OFF");
    Expect(Command(engine, "DISP:WIND2?") == "0\n" && fixture.service.Display().diagrams.size() == 1U,
        "窗口 OFF 未删除指定视图");
    Command(engine, "DISP:WIND1 OFF");
    Expect(Command(engine, "SYST:ERR?").find("-222,") == 0U, "项目子集必须保护最后一个图窗");
}

void SparseFeedAndUnsupportedQueriesAreAtomic(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "CALC:PAR:DEF:EXT 'Hidden','S11'");
    Command(engine, "DISP:WIND1:TRAC3:FEED 'Hidden'");
    Expect(Command(engine, "DISP:WIND1:CAT?") == "\"1,3\"\n", "FEED 应保留指定稀疏槽身份");
    Command(engine, "DISP:WIND1:TRAC1:DEL;:DISP:WIND1:TRAC3:SEL");
    Expect(Command(engine, "DISP:WIND1:CAT?;:CALC:PAR:SEL?") == "\"3\";\"Hidden\"\n",
        "删除第一槽不得把第三槽压缩为第一槽");
    Command(engine, "DISP:WIND1:TRAC128:FEED 'Trc1'");
    Expect(Command(engine, "DISP:WIND1:CAT?") == "\"3,128\"\n", "最大合法稀疏槽未显示");
    const auto revision = fixture.service.Display().revision;
    Command(engine, "DISP:WIND1:TRAC129:FEED 'Hidden'");
    Expect(Command(engine, "SYST:ERR?").find("-222,") == 0U, "超出显示槽资源预算应明确拒绝");
    for (const auto &line : {"DISP:WIND1:TRAC1:FEED?", "DISP:WIND1:TRAC1:DEL?", "DISP:WIND1:CAT ON"}) {
        Command(engine, line);
        Expect(Command(engine, "SYST:ERR?").find("-113,") == 0U, "只写或只读命令不得反向执行");
    }
    Expect(fixture.service.Display().revision == revision, "非法命令留下了部分图窗状态");
}

void WebOwnedWindowsAreVisibleToObserver(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    Expect(fixture.service.TryAcquireControl("WEB:controller"), "Web 未取得控制权");
    VnaService::Measurement measurement;
    measurement.name = "Second";
    Expect(measurement.SetParameter("S11"), "合法测量参数未解析");
    Await(fixture.service,
        fixture.service.DefineMeasurement(measurement, fixture.service.Settings().revision, "WEB:controller"));
    VnaDisplay::DisplayChange change;
    change.kind = VnaDisplay::DisplayChangeKind::DIAGRAM_ADD;
    change.diagram = 2U;
    Await(fixture.service, fixture.service.ApplyDisplay(change, fixture.service.Display().revision, "WEB:controller"));
    change.kind = VnaDisplay::DisplayChangeKind::SELECT;
    change.name = "Second";
    change.hasSlot = true;
    change.slot = 1U;
    Await(fixture.service, fixture.service.ApplyDisplay(change, fixture.service.Display().revision, "WEB:controller"));
    VnaScpi::ScpiEngine observer(fixture.service);
    const auto revision = fixture.service.Display().revision;
    Expect(Command(observer, "DISP:WIND2:STAT?;CAT?;SIZE?;TRAC1:STAT?;Y:SCAL:PDIV?").find("1;\"1\";NORM;1;") == 0U,
        "SCPI 观察者未读取 Web 创建的图窗局部状态");
    Expect(!observer.HasControl() && fixture.service.ControlOwner() == "WEB:controller" &&
               fixture.service.Display().revision == revision,
        "观察图窗查询改变了控制权或共享显示状态");
    Expect(!observer.SubmitLine("DISP:WIND2:CAT?;:DISP:WIND2:TRAC1:DEL"), "混合写入消息不得通过只读观察准入");
    fixture.service.ReleaseControl("WEB:controller");
}

void LogMagnitudeScaleReadsDecadesAndPreservesReference(const std::string &driver)
{
    VnaScpiTest::NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    VnaDisplay::DisplayChange change;
    change.kind = VnaDisplay::DisplayChangeKind::FORMAT;
    change.format = VnaDisplay::DisplayFormat::LOG_MAGNITUDE;
    // 原生格式通过类型化后端入口设置，不为 PNA 增造格式命令词。
    Await(fixture.service, fixture.service.ApplyDisplay(change));
    const double reference = std::pow(10.0, 0.2);
    Expect(std::fabs(std::stod(Command(engine, "DISP:WIND1:TRAC1:Y:PDIV?")) - 0.4) < 1e-12,
        "Log Mag 每格尺度应按 decade 读回");
    Expect(std::fabs(std::stod(Command(engine, "DISP:WIND1:TRAC1:Y:RLEV?")) - reference) < 1e-12,
        "Log Mag 参考值应按对数坐标还原为正幅值");
    Command(engine, "DISP:WIND1:TRAC1:Y:PDIV 0.5");
    Expect(std::fabs(std::stod(Command(engine, "DISP:WIND1:TRAC1:Y:PDIV?")) - 0.5) < 1e-12,
        "Log Mag 修改每格尺度后应读回生效 decade");
    Expect(std::fabs(std::stod(Command(engine, "DISP:WIND1:TRAC1:Y:RLEV?")) - reference) < 1e-12,
        "Log Mag 修改每格尺度不得移动原有幅值参考");
    Expect(Command(engine, "SYST:ERR?").find("0,") == 0U, "合法 Log Mag 尺度操作产生错误");
}

}  // 匿名命名空间

int main(int argc, char **argv)
{
    if (argc != 2) {
        return 2;
    }
    VnaTest::ScopedSimulatorEnvironment environment("golden", nullptr);
    try {
        WindowLocalSlotsAndViewDeletion(argv[1]);
        SelectionDoesNotFeedHiddenMeasurement(argv[1]);
        SparseFeedAndUnsupportedQueriesAreAtomic(argv[1]);
        WebOwnedWindowsAreVisibleToObserver(argv[1]);
        LogMagnitudeScaleReadsDecadesAndPreservesReference(argv[1]);
        std::cout << "PASS SCPI 显式图窗与局部迹线槽\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
