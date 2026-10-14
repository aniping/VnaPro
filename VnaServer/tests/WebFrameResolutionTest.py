"""验证网页常用一至四迹线完整保留扫频采样，并与 SCPI 读取相同 IQ。"""

import json
import os
from pathlib import Path
import statistics
import sys
import time

from GoldenIqIntegrationTest import listening_server
from WebHttpIntegrationTest import HttpClient, accepted, connect_scpi, wait_operation
from server_startup import isolated_server


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
    print(f"完整 {points} 点，一至四迹线 HTTP 中位数 {statistics.median(timings) * 1000:.2f} ms")


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
                assert len(raw) < 16 * 1024 * 1024
                assert frame["displayPointCount"] == 10001 and not frame["decimated"]
                assert all(trace["projection"]["format"] == format_name for trace in frame["traces"])
                print(f"完整 10001 点四迹线 {format_name} 响应 {len(raw)} 字节")


if __name__ == "__main__":
    main()
