"""验证网页常用一至四迹线完整保留扫频采样，并与 SCPI 读取相同 IQ。"""

import base64
import json
import math
import os
from pathlib import Path
import statistics
import struct
import sys
import time

from GoldenIqIntegrationTest import listening_server
from WebHttpIntegrationTest import HttpClient, accepted, connect_scpi, wait_operation
from server_startup import isolated_server


def unpack(value):
    """独立解码协议字节，不能依赖 C++ 或前端实现计算预期值。"""
    if isinstance(value, list):
        return [unpack(item) for item in value]
    if not isinstance(value, dict):
        return value
    if "codec" not in value:
        return {key: unpack(item) for key, item in value.items()}
    count, codec = value["count"], value["codec"]
    data = base64.b64decode(value["data"], validate=True)
    assert count >= 0 and len(data) == count * (8 if codec == "f64le" else 1)
    if codec == "f64le":
        return [None if math.isnan(item[0]) else item[0] for item in struct.iter_unpack("<d", data)]
    if codec == "u8":
        assert all(item in (0, 1) for item in data)
        return [bool(item) for item in data]
    assert codec == "enum8"
    return [value["dictionary"][item] for item in data]


def packed_frame(http, names):
    path = "/api/v1/frame?encoding=packed&" + "&".join("trace=" + name for name in names)
    raw, _ = http.request("GET", path, raw=True)
    return unpack(json.loads(raw)), len(raw)


def check_full_frame(http, client, points, names):
    assert client.query(f"SENS1:SWE:POIN {points};*OPC?") == "+1"
    assert client.query("SENS1:SWE:MODE SING;*OPC?") == "+1"
    expected_axis = [float(value) for value in client.query("SENS1:X?").split(",")]
    expected_iq = [float(value) for value in client.query(
        'CALC1:PAR:SEL "Trc1";:CALC1:DATA? RDATA').split(",")]
    assert len(expected_axis) == points and len(expected_iq) == points * 2
    timings = []
    for count in range(1, 5):
        started = time.perf_counter()
        frame = http.frame(names[:count])
        timings.append(time.perf_counter() - started)
        assert frame["current"] and not frame["decimated"], (
            points, count, frame.get("displayPointCount"), frame.get("decimated"))
        assert frame["originalPointCount"] == frame["displayPointCount"] == points
        assert frame["frequencyHz"] == expected_axis
        assert frame["sampleIndexes"] == list(range(points))
        assert len(frame["traces"]) == count
        for trace in frame["traces"]:
            assert trace["real"] == expected_iq[::2] and trace["imag"] == expected_iq[1::2]
            assert len(trace["projection"]["values"]) == points
        decoded, _ = packed_frame(http, names[:count])
        assert decoded == frame, "紧凑表示必须保留同一 HOLD 帧的所有字段、采样和投影"
    print(f"完整 {points} 点，一至四迹线 HTTP 中位数 {statistics.median(timings) * 1000:.2f} ms")


def check_large_catalog(http, client):
    for number in range(5, 129):
        assert client.query(f'CALC1:PAR:DEF:EXT "Trc{number}","S21";*OPC?') == "+1"
    original = http.frame(["Trc1"])
    expected_real, expected_imag = original["traces"][0]["real"], original["traces"][0]["imag"]
    for count in (16, 128):
        started = time.perf_counter()
        frame, size = packed_frame(http, [f"Trc{index}" for index in range(1, count + 1)])
        assert frame["displayPointCount"] == frame["originalPointCount"] == 10001
        assert not frame["decimated"] and frame["current"]
        assert frame["sampleIndexes"] == list(range(10001))
        assert frame["frequencyHz"] == original["frequencyHz"]
        for key in ("frameSeq", "runId", "revision", "measurementRevision", "displayRevision"):
            assert frame[key] == original[key], "单次多迹线响应不能跨帧拼接"
        assert len(frame["traces"]) == count
        assert all(trace["real"] == expected_real and trace["imag"] == expected_imag
                   and len(trace["projection"]["values"]) == 10001 for trace in frame["traces"])
        print(f"完整 10001 点 {count} 迹线紧凑响应 {size} 字节，解码总耗时 {(time.perf_counter() - started) * 1000:.2f} ms")


def main():
    assert len(sys.argv) in (3, 4), "usage: WebFrameResolutionTest.py <server> <vn10-driver> [web-assets]"
    environment = dict(os.environ, VNAPRO_SIM_MODE="noise", VNAPRO_SIM_SEED="42")
    assets = Path(sys.argv[3]) if len(sys.argv) == 4 else None
    with isolated_server(sys.argv[1], sys.argv[2], environment, board="tdc1vn10", web_root=assets) as bundle:
        # 本验收使用独立配置和随机端口，避免修改正在运行的仪器。
        configuration = bundle.config.read_text(encoding="utf-8")
        configuration = configuration.replace("defaultSweepMode: CONT", "defaultSweepMode: HOLD")
        bundle.config.write_text(configuration, encoding="utf-8")
        with listening_server(bundle) as ports:
            http, client = HttpClient(ports["http"]), connect_scpi(ports["scpi"])
            try:
                assert client.query("*CLS;:SENS1:SWE:MODE HOLD;*OPC?") == "+1"
                names = [f"Trc{index}" for index in range(1, 5)]
                for name in names[1:]:
                    assert client.query(f'CALC1:PAR:DEF:EXT "{name}","S21";*OPC?') == "+1"
                for points in (201, 4000, 8000, 10001):
                    check_full_frame(http, client, points, names)
                check_large_catalog(http, client)
                assert client.query("SYST:ERR?").startswith("0,")
            finally:
                client.close()
            # 点数增加也必须覆盖较大的圆图及实虚投影，不能在编码阶段截断。
            token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
            for format_name in ("smith", "invSmith", "polar", "realimag"):
                for name in names:
                    body = {
                        "action": "format", "traceName": name, "format": format_name,
                        "requestId": f"format-{format_name}-{name}",
                        "expectedDisplayRevision": http.status()["display"]["revision"],
                    }
                    wait_operation(http, accepted(http, "display", body, token), token)
                raw, _ = http.request("GET", "/api/v1/frame?" + "&".join(
                    "trace=" + name for name in names), raw=True)
                frame = json.loads(raw)
                assert len(raw) < 256 * 1024 * 1024
                assert frame["displayPointCount"] == 10001 and not frame["decimated"]
                assert all(trace["projection"]["format"] == format_name for trace in frame["traces"])
                decoded, _ = packed_frame(http, names)
                assert decoded == frame, "所有圆图及实虚投影的普通和紧凑表示必须逐值一致"
                print(f"完整 10001 点四迹线 {format_name} 响应 {len(raw)} 字节")


if __name__ == "__main__":
    main()
