"""验证生产默认能力、二端口配置及 HTTP/SCPI 的实际边界约束。"""

import math
import os
from pathlib import Path
import sys
import tempfile
import time

from GoldenIqIntegrationTest import running_server
from WebHttpIntegrationTest import HttpClient, connect_scpi, wait_no_owner


MIN_HZ = 10000000
MAX_HZ = 8000000000
DEFAULT_POINTS = 201


def status_defaults(http, port_count):
    status = http.status()
    assert status["identity"] == f"VnaPro,NativeSimulator,{port_count}PORT,1.0", status
    assert status["capabilities"] == {
        "portCount": port_count, "minFrequencyHz": MIN_HZ, "maxFrequencyHz": MAX_HZ,
        "minSweepPoints": 2, "maxSweepPoints": 10001,
    }, status["capabilities"]
    config = status["config"]
    assert (config["startHz"], config["stopHz"], config["points"], config["ifBandwidthHz"]) == (
        MIN_HZ, MAX_HZ, DEFAULT_POINTS, 1000,
    ), config
    assert status["measurements"] == [{
        "name": "Trc1", "receiverKind": "b", "receiverPort": 2, "sourcePort": 1,
        "parameter": "S21", "kind": "sParameter", "unit": "1", "calibration": "uncalibrated",
    }], status["measurements"]
    assert config["sweepMode"] == "HOLD", config
    assert not http.frame()["available"], "初始观察不能自动启动采集"


def assert_frame(http, client):
    assert int(client.query("TRIG:SOUR IMM;:SENS1:SWE:MODE SING;*OPC?")) == 1
    frame = http.frame()
    assert frame["available"] and frame["current"], frame
    status = http.status()
    assert frame["revision"] == status["acquisitionRevision"]
    assert frame["measurementRevision"] == status["revision"]
    assert frame["originalPointCount"] == frame["displayPointCount"] == DEFAULT_POINTS
    expected_axis = [MIN_HZ + index * ((MAX_HZ - MIN_HZ) // (DEFAULT_POINTS - 1))
                     for index in range(DEFAULT_POINTS)]
    assert expected_axis[-1] == MAX_HZ
    assert frame["frequencyHz"] == expected_axis, frame["frequencyHz"]
    assert [float(item) for item in client.query("SENS1:X?").split(",")] == expected_axis
    for trace in frame["traces"]:
        assert trace["available"] and all(trace["valid"]), trace
        assert len(trace["real"]) == len(trace["imag"]) == DEFAULT_POINTS
        assert all(math.isfinite(item) for item in trace["real"] + trace["imag"])
        values = client.query(f'CALC1:PAR:SEL "{trace["name"]}";:CALC1:DATA? RDATA')
        actual = [float(item) for item in values.split(",")]
        expected = [item for pair in zip(trace["real"], trace["imag"]) for item in pair]
        assert actual == expected, "HTTP 和 SCPI 必须读取相同的完整帧"
    return frame


def scpi_rejections(http, client, port_count):
    baseline = http.status()
    for command in ("SENS1:FREQ:STAR 9999999", "SENS1:FREQ:STOP 8000000001"):
        assert int(client.query(command + ";*OPC?")) == 1
        assert client.query("SYST:ERR?").startswith("-222,"), command
        current = http.status()
        assert current["revision"] == baseline["revision"] and current["config"] == baseline["config"]
    if port_count == 2:
        for parameter in ("a3,1", "b4,1", "a1,3", "b1,4"):
            client.query(f'CALC1:PAR:DEF:EXT "OutOfProfile","{parameter}";*OPC?')
            assert client.query("SYST:ERR?").startswith("-222,"), parameter
            assert http.status()["revision"] == baseline["revision"]
            assert "OutOfProfile" not in {item["name"] for item in http.status()["measurements"]}


def define_supported(http, client, port_count):
    if port_count == 4:
        definitions = (("Port3", "b3,3"), ("Port4", "b4,4"))
    else:
        definitions = (("ReverseTransmit", "S12"), ("ReverseReflect", "S22"))
    for name, parameter in definitions:
        assert int(client.query(f'CALC1:PAR:DEF:EXT "{name}","{parameter}";*OPC?')) == 1
        assert client.query("SYST:ERR?").startswith("0,"), parameter
    frame = assert_frame(http, client)
    traces = {trace["name"]: trace for trace in frame["traces"]}
    assert all(name in traces for name, _ in definitions), traces
    if port_count == 4:
        assert traces["Port3"]["sourcePort"] == traces["Port3"]["receiverPort"] == 3
        assert traces["Port4"]["sourcePort"] == traces["Port4"]["receiverPort"] == 4
    else:
        for name, parameter in definitions:
            assert traces[name]["sourcePort"] == 2 and traces[name]["parameter"] == parameter
            assert traces[name]["calibration"] == "uncalibrated"


def http_rejected(http, token, path, body):
    baseline = http.status()
    body = dict(body, expectedRevision=baseline["revision"])
    response = http.request("POST", "/api/v1/" + path, body, token, expected=(202, 409))
    if "error" in response:
        assert response["error"]["code"] == "operation_rejected", response
    else:
        deadline = time.monotonic() + 8
        while True:
            result = http.request("GET", "/api/v1/operations/" + response["operationId"], token=token)
            if result["state"] != "pending":
                assert result["state"] == "failed" and not result["result"]["ok"], result
                assert result["result"]["errorCode"] == -222, result
                break
            assert time.monotonic() < deadline, result
            time.sleep(0.002)
    current = http.status()
    assert current["revision"] == baseline["revision"] and current["config"] == baseline["config"]
    assert current["measurements"] == baseline["measurements"], "非法配置不能改变测量目录"


def http_rejections(http, port_count):
    wait_no_owner(http)
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    try:
        for key, value in (("startHz", MIN_HZ - 1), ("stopHz", MAX_HZ + 1)):
            http_rejected(http, token, "config", {key: value, "requestId": "invalid-" + key})
        if port_count == 2:
            for receiver, source in ((3, 1), (4, 1), (1, 3), (1, 4)):
                http_rejected(http, token, "measurements", {
                    "name": "OutOfProfile", "receiverKind": "b", "receiverPort": receiver, "sourcePort": source,
                    "requestId": f"invalid-port-{receiver}-{source}",
                })
    finally:
        http.request("POST", "/api/v1/control/release", {}, token)


def exercise(config, port_count):
    environment = dict(os.environ, VNAPRO_SIM_MODE="golden")
    environment.pop("VNAPRO_SIM_SEED", None)
    with running_server(sys.argv[1], sys.argv[2], environment, config=config) as ports:
        http = HttpClient(ports["http"])
        status_defaults(http, port_count)
        client = connect_scpi(ports["scpi"])
        try:
            assert client.query("*IDN?") == f"VnaPro,NativeSimulator,{port_count}PORT,1.0"
            assert int(client.query("*CLS;:SENS1:SWE:MODE HOLD;*OPC?")) == 1
            assert int(client.query("SENS1:SWE:POIN?")) == DEFAULT_POINTS
            assert float(client.query("SENS1:FREQ:STAR?")) == MIN_HZ
            assert float(client.query("SENS1:FREQ:STOP?")) == MAX_HZ
            assert client.query("CALC1:PAR:CAT:EXT?") == '"Trc1,S21"'
            assert client.query("CALC1:PAR:SEL?") == '"Trc1"'
            assert client.query("SENS1:SWE:MODE?") == "HOLD"
            assert_frame(http, client)
            scpi_rejections(http, client, port_count)
            define_supported(http, client, port_count)
        finally:
            client.close()
        http_rejections(http, port_count)


def main():
    assert len(sys.argv) == 3, "usage: InstrumentConfigurationIntegrationTest.py <server> <driver>"
    # 无显式配置路径时实际读取服务生成的生产 YAML，不能由测试手工替代默认值。
    exercise(None, 4)
    with tempfile.TemporaryDirectory(prefix="vnapro-instrument-") as temporary:
        config = Path(temporary) / "two-port.yml"
        config.write_text("instrument: {portCount: 2}\nlog: {file: {enabled: false}}\n", encoding="utf-8")
        exercise(config, 2)
    print("仪器配置验收通过：四端口默认、二端口能力、10 MHz 至 8 GHz、201 点、HTTP/SCPI 边界")


if __name__ == "__main__":
    main()
