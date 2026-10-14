"""验证真实类型的 VN10 全链路采集；有序 Close 由 Core ABI 探针测试覆盖。"""

import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

from SParameterIntegrationTest import read_complex, trace_complex
from WebHttpIntegrationTest import HttpClient, connect_scpi, wait_no_owner, wait_operation
from server_startup import isolated_server, listening_ports, replace_section


SENTINELS = {
    "VNAPRO_SIM_MODE": "hardware-probe-mode-unchanged",
    "VNAPRO_SIM_SEED": "hardware-probe-seed-unchanged",
    "VNAPRO_SIM_NOISE_AMPLITUDE": "hardware-probe-amplitude-unchanged",
}
POINTS = 130


def command(client, text):
    assert int(client.query(text + ";*OPC?")) == 1, text
    assert client.query("SYST:ERR?").startswith("0,"), text


def expected_wave(sweep, source, receiver, wave):
    """独立求值，刻意与正式模拟信号模型无关；每源/频点/接收波均不同。"""
    offset = (receiver - 1) * 4 + (2 if wave == "b" else 0)
    return [complex(sweep * 1000000 + source * 100000 + point * 100 + offset,
                    sweep * 1000000 + source * 100000 + point * 100 + offset + 1)
            for point in range(POINTS)]


def compare(actual, expected, label):
    assert len(actual) == len(expected) == POINTS, label
    for point, (value, reference) in enumerate(zip(actual, expected)):
        assert math.isclose(value.real, reference.real, rel_tol=1e-12, abs_tol=1e-12), (label, point)
        assert math.isclose(value.imag, reference.imag, rel_tol=1e-12, abs_tol=1e-12), (label, point)


def held_data(http, client, port_count, sweep):
    names = [f"Trc{index}" for index in range(1, port_count ** 2 + 1)]
    frame = http.frame(names)
    assert frame["available"] and frame["current"] and not frame["decimated"], frame
    assert frame["frequencyHz"] == [10000000 + point * 1000000 for point in range(POINTS)]
    assert frame["originalPointCount"] == frame["displayPointCount"] == POINTS
    traces = {trace["name"]: trace for trace in frame["traces"]}
    assert set(traces) == set(names)
    for source in range(1, port_count + 1):
        reference = read_complex(client, f"RawA{source}")
        assert reference == expected_wave(sweep, source, source, "a"), f"原始 A{source} 被改变"
        for receiver in range(1, port_count + 1):
            numerator = read_complex(client, f"RawB{receiver}{source}")
            assert numerator == expected_wave(sweep, source, receiver, "b"), f"原始 B{receiver}{source} 被改变"
            ratio = [b / a for a, b in zip(reference, numerator)]
            name = f"Trc{(receiver - 1) * port_count + source}"
            assert traces[name]["parameter"] == f"S{receiver}{source}"
            compare(trace_complex(traces[name]), ratio, name + " HTTP 原始波比值")
            compare(read_complex(client, name), ratio, name + " SCPI 原始波比值")
    later = http.frame(names)
    assert all(later[key] == frame[key] for key in ("revision", "runId", "frameSeq"))
    assert client.query("SENS1:SWE:MODE?") == "HOLD"


def trace_records(path):
    return [line.split() for line in path.read_text(encoding="utf-8").splitlines()]


def assert_scan_order(path, port_count):
    records = trace_records(path)
    assert len({row[0] for row in records}) == 1, "驱动调用没有保持单工作线程"
    active, next_point, starts = False, 0, 0
    for row in records:
        if row[1] == "START":
            assert not active, "尚未读完上一轮完整扫描就调用下一次 Start"
            active, next_point, starts = True, 0, starts + 1
        elif row[1] in ("GET_BEGIN", "GET_END"):
            first, count, capacity = map(int, row[2:])
            assert active and first == next_point
            assert count == min(64, POINTS - first) and capacity == count * port_count * 16
            if row[1] == "GET_END":
                next_point += count
                active = next_point != POINTS
        assert row[1] not in ("GET_FAIL", "UNLOAD", "UNSAFE_UNLOAD"), row
    assert not active and next_point == POINTS and starts >= 4
    return starts


def exercise(server, driver, port_count):
    environment = dict(os.environ)
    for name in (*SENTINELS, "VNAPRO_TEST_BOARD_FAIL", "VNAPRO_TEST_BOARD_FAIL_AT", "VNAPRO_TEST_BOARD_GATE_AT"):
        environment.pop(name, None)
    with isolated_server(server, driver, environment, board="tdc1vn10") as bundle:
        configuration = bundle.config.read_text(encoding="utf-8")
        sections = {
            "driver": {"type": "hardware", "board": "tdc1vn10", "library": Path(driver).name,
                       "simulator": {"mode": "invalid", "seed": "invalid", "noiseAmplitude": -1}},
            "instrument": {"portCount": port_count, "frequency": {"minHz": 10000000, "maxHz": 8000000000},
                           "sweep": {"defaultStartHz": 10000000, "defaultStopHz": 139000000,
                                     "defaultPoints": POINTS, "defaultIfBandwidthHz": 1000,
                                     "defaultPowerRaw": 0, "defaultSweepMode": "HOLD"}},
            "log": {"level": "INFO", "console": {"enabled": True}, "file": {"enabled": False}},
        }
        for name, values in sections.items():
            configuration = replace_section(configuration, name, name + ": " + json.dumps(values) + "\n")
        bundle.config.write_text(configuration, encoding="utf-8")
        trace = bundle.config.parent / "driver-trace.log"
        bundle.environment.update(SENTINELS, VNAPRO_TEST_BOARD_TRACE=str(trace))
        output = bundle.config.parent / "server.log"
        with output.open("wb") as stream:
            process = subprocess.Popen([bundle.executable], env=bundle.environment, cwd=bundle.config.parent,
                                       stdout=stream, stderr=subprocess.STDOUT)
            client = None
            try:
                deadline = time.monotonic() + 10
                while True:
                    text = output.read_text(encoding="utf-8")
                    ports = listening_ports(text)
                    assert process.poll() is None, text
                    if set(ports) == {"http", "scpi"}:
                        break
                    assert time.monotonic() < deadline, text
                    time.sleep(0.01)
                http = HttpClient(ports["http"])
                status = http.status()
                assert status["backend"] == "HardwareDriver" and status["config"]["sweepMode"] == "HOLD"
                token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
                try:
                    status = http.status(token)
                    result = http.request("POST", "/api/v1/measurement-actions/all-sparameters", {
                        "confirmed": True, "requestId": "hardware-all-sparameters",
                        "expectedRevision": status["revision"],
                        "expectedDisplayRevision": status["display"]["revision"],
                    }, token, expected=202)
                    wait_operation(http, result["operationId"], token)
                finally:
                    http.request("POST", "/api/v1/control/release", {}, token)
                wait_no_owner(http)
                client = connect_scpi(ports["scpi"])
                assert client.query("*IDN?").startswith("VnaPro,TDC1VN10,"), "硬件路径仍返回模拟器身份"
                revision = http.status()["acquisitionRevision"]
                for source in range(1, port_count + 1):
                    command(client, f'CALC1:PAR:DEF:EXT "RawA{source}","a{source},{source}"')
                    for receiver in range(1, port_count + 1):
                        command(client, f'CALC1:PAR:DEF:EXT "RawB{receiver}{source}","b{receiver},{source}"')
                assert http.status()["acquisitionRevision"] == revision, "新增验证定义错误地重复下发硬件计划"
                command(client, "FORM:DATA ASC,0;:SENS1:SWE:MODE SING")
                held_data(http, client, port_count, 1)
                command(client, "SENS1:SWE:MODE CONT")
                deadline, sequences = time.monotonic() + 8, set()
                while len(sequences) < 3:
                    frame = http.frame(["Trc1"])
                    if frame["available"] and frame["current"]:
                        sequences.add(frame["frameSeq"])
                    assert time.monotonic() < deadline, "硬件连续扫描没有持续发布完整帧"
                    time.sleep(0.005)
                command(client, "SENS1:SWE:MODE HOLD")
                sweep = assert_scan_order(trace, port_count)
                held_data(http, client, port_count, sweep)
                assert "模拟驱动启动配置" not in output.read_text(encoding="utf-8")
            finally:
                if client is not None:
                    client.close()
                # terminate 只回收隔离测试进程，不能作为驱动 Close 或扫描取消的证据。
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)


def main():
    assert len(sys.argv) == 3, "usage: HardwareBoardAcquisitionIntegrationTest.py <server> <VN10 ABI probe>"
    for port_count in (2, 4):
        exercise(sys.argv[1], sys.argv[2], port_count)
    print("VN10 硬件类型双/四源、分段拼接、原始 IQ/S 参数、HTTP/SCPI、连续/HOLD 及模拟参数隔离通过")


if __name__ == "__main__":
    main()
