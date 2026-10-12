/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"
#include "scpi/scpi_engine.h"
#include "vna_service/instrument_service.h"

namespace {

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string Query(VnaScpi::ScpiEngine &engine, const std::string &line)
{
    Expect(engine.SubmitLine(line), "观察连接拒绝合法查询：" + line);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        engine.Poll();
        auto output = engine.TakeOutput();
        if (!output.empty()) {
            return output;
        }
        Expect(std::chrono::steady_clock::now() < deadline, "观察查询没有返回响应");
        std::this_thread::yield();
    } while (true);
}

void ObserveWebControllerAndRejectWholeMessages()
{
    VnaServiceTest::Fixture fixture;
    Expect(fixture.service.TryAcquireControl("WEB:controller"), "Web 控制权未取得");
    VnaScpi::ScpiEngine observer(fixture.service);
    Expect(!observer.HasControl(), "观察者不应取得 Web 控制权");
    Expect(Query(observer, "*IDN?;:CALC:PAR:SEL?;:CALC:FORM?").find(";\"Trc1\";MLOG\n") != std::string::npos,
        "观察者没有读取统一仪器状态");
    const auto displayRevision = fixture.service.Display().revision;
    Expect(!observer.SubmitLine("*IDN?;:CALC:FORM REAL"), "混合消息不应部分执行");
    observer.Poll();
    Expect(observer.TakeOutput().empty() && fixture.service.Display().revision == displayRevision,
        "拒绝消息执行了前缀或更改显示");
    Expect(fixture.service.ErrorCount() == 0U && fixture.service.ControlOwner() == "WEB:controller",
        "观察者请求污染了控制者错误队列或控制权");
    observer.Disconnect();
    observer.Poll();
    Expect(observer.CanReleaseControl() && fixture.service.ControlOwner() == "WEB:controller",
        "观察者断开不应释放 Web 控制权");
    fixture.service.ReleaseControl("WEB:controller");
}

void ObserverCannotConsumeRegistersOrErrors()
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine writer(fixture.service);
    VnaScpi::ScpiEngine observer(fixture.service);
    fixture.service.RecordError(-113, "writer diagnostic");
    const auto count = fixture.service.ErrorCount();
    for (const auto &line : {"*IDN?;*ESR?", "*STB?;:SYST:ERR?", "SYST:ERR:NEXT?", "*CLS"}) {
        Expect(!observer.SubmitLine(line), "观察者不应执行读清、出队或写请求");
        observer.Poll();
        Expect(observer.TakeOutput().empty() && fixture.service.ErrorCount() == count,
            "观察者破坏了控制者错误队列或部分执行混合消息");
    }
    Expect(Query(observer, "*ESE?;*SRE?;*STB?;:SYST:ERR:COUN?").find(";1\n") != std::string::npos,
        "允许的只读状态查询未返回");
    observer.Disconnect();
    observer.Poll();
    Expect(writer.HasControl() && fixture.service.ControlOwner() == "SCPI", "观察者误伤真实 SCPI writer");
    Expect(fixture.service.ReadEventRegister() & 32U, "观察者错误地读清了 writer ESR");
}

void ObserverDoesNotUpgradeAfterControllerLeaves()
{
    VnaServiceTest::Fixture fixture;
    Expect(fixture.service.TryAcquireControl("WEB:controller"), "Web 控制权未取得");
    VnaScpi::ScpiEngine observer(fixture.service);
    fixture.service.ReleaseControl("WEB:controller");
    Expect(Query(observer, "SENS:SWE:POIN?") == "201\n", "既有 observer 查询不应因 writer 离开失效");
    Expect(!observer.HasControl() && fixture.service.ControlOwner().empty(), "观察连接不能自动升级");
    Expect(!observer.SubmitLine("SENS:SWE:POIN 301"), "观察连接不能在空闲时写入");
    VnaScpi::ScpiEngine next(fixture.service);
    Expect(next.HasControl(), "新连接应可获取已释放的 writer 控制权");
    observer.Disconnect();
    observer.Poll();
    Expect(next.HasControl() && fixture.service.ControlOwner() == "SCPI", "旧 observer 断开释放了新 writer");
}

void UnknownAndMalformedQueriesDoNotPolluteInstrument()
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine writer(fixture.service);
    VnaScpi::ScpiEngine observer(fixture.service);
    for (const auto &line : {"*IDN?;:UNKNOWN?", "*IDN?;?", "CALC:PAR:SEL? 'unterminated"}) {
        Expect(!observer.SubmitLine(line), "未知或畸形观察消息不应入队");
        observer.Poll();
        Expect(observer.TakeOutput().empty() && fixture.service.ErrorCount() == 0U, "观察解析错误不应影响共享错误队列");
    }
}

void QueriesRemainResponsiveAndOpcWaitsForFiniteOperation()
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine writer(fixture.service);
    fixture.device->Arm();
    Expect(writer.SubmitLine("SENS:SWE:MODE SING"), "有限扫描未受理");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        writer.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "受控扫描未进入阻塞设备");
        std::this_thread::yield();
    }
    VnaScpi::ScpiEngine observer(fixture.service);
    Expect(
        Query(observer, "*IDN?").find("VnaPro,") == 0U && !observer.HasPending(), "普通查询不应等待 writer 的有限扫描");
    VnaScpi::ScpiEngine idleObserver(fixture.service);
    idleObserver.Disconnect();
    idleObserver.Poll();
    Expect(idleObserver.CanReleaseControl() && writer.HasControl() && fixture.service.ControlOwner() == "SCPI",
        "空闲观察连接关闭不应等待或释放 writer");
    Expect(observer.SubmitLine("*OPC?"), "观察 OPC 查询未受理");
    observer.Poll();
    Expect(observer.TakeOutput().empty() && observer.HasPending(), "观察 OPC 不能在扫描完成前返回");
    fixture.device->Release();
    while (writer.HasPending()) {
        writer.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "有限扫描未结束");
        std::this_thread::yield();
    }
    observer.Poll();
    Expect(observer.TakeOutput() == "+1\n", "观察 OPC 未在已受理有限操作结束后返回");
}

}  // 匿名命名空间

int main()
{
    try {
        ObserveWebControllerAndRejectWholeMessages();
        ObserverCannotConsumeRegistersOrErrors();
        ObserverDoesNotUpgradeAfterControllerLeaves();
        UnknownAndMalformedQueriesDoNotPolluteInstrument();
        QueriesRemainResponsiveAndOpcWaitsForFiniteOperation();
        std::cout << "PASS SCPI 只读观察与独占写控制权\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
