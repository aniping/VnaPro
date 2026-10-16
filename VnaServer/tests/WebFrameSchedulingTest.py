"""验证大帧 HTTP 编码及时推进，同时保留控制查询与慢客户端之间的公平性。"""

from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import socket
import statistics
import sys
import tempfile
import threading
import time

from GoldenIqIntegrationTest import running_server
from WebHttpIntegrationTest import HttpClient, accepted, connect_scpi, wait_operation


POINT_COUNT = 4000
DISPLAY_POINT_COUNT = POINT_COUNT
FRAME_MEDIAN_LIMIT = 1.0
CONTROL_MEDIAN_LIMIT = 0.25


def configure(http):
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    for number, parameter in enumerate(("S11", "S12", "S22"), start=2):
        body = {
            "requestId": f"frame-define-{number}",
            "expectedRevision": http.status()["revision"],
            "name": f"Trc{number}",
            "parameter": parameter,
        }
        wait_operation(http, accepted(http, "measurements", body, token), token)
        display = {
            "requestId": f"frame-display-{number}",
            "expectedDisplayRevision": http.status()["display"]["revision"],
            "action": "displayed",
            "traceName": f"Trc{number}",
            "enabled": True,
        }
        wait_operation(http, accepted(http, "display", display, token), token)
    config = {
        "requestId": "frame-configure-4000",
        "expectedRevision": http.status()["revision"],
        "points": POINT_COUNT,
    }
    wait_operation(http, accepted(http, "config", config, token), token)
    action = {"requestId": "frame-start-continuous", "action": "continuous"}
    wait_operation(http, accepted(http, "operations", action, token), token)
    return token


def validate_frame(frame):
    assert frame["available"] and frame["current"], "必须返回当前完整采集帧"
    assert frame["originalPointCount"] == POINT_COUNT
    assert frame["displayPointCount"] == DISPLAY_POINT_COUNT
    assert len(frame["traces"]) == 4
    assert frame["sampleIndexes"][0] == 0 and frame["sampleIndexes"][-1] == POINT_COUNT - 1
    for trace in frame["traces"]:
        assert trace["available"] and len(trace["real"]) == DISPLAY_POINT_COUNT
        assert len(trace["projection"]["values"]) == DISPLAY_POINT_COUNT


def measure_frames(http, barrier):
    barrier.wait()
    durations, identities = [], []
    for _ in range(3):
        started = time.perf_counter()
        frame = http.frame()
        durations.append(time.perf_counter() - started)
        validate_frame(frame)
        identities.append(int(frame["frameSeq"]))
    assert identities == sorted(identities), "完整帧序号不能回退，快速读取同一帧是合法的"
    return durations


def measure_status(http, token, barrier=None):
    if barrier is not None:
        barrier.wait()
    durations = []
    for _ in range(12):
        started = time.perf_counter()
        status = http.status(token)
        durations.append(time.perf_counter() - started)
        assert status["control"]["hasControl"] and status["config"]["sweepMode"] == "CONT"
        time.sleep(0.01)
    return durations


def measure_scpi(client, barrier):
    barrier.wait()
    durations = []
    for _ in range(12):
        started = time.perf_counter()
        assert client.query("*IDN?").startswith("VnaPro,")
        durations.append(time.perf_counter() - started)
        time.sleep(0.01)
    return durations


def check_acquisition_progress(http, baseline):
    # 响应可快于单次扫描；采集进展使用独立时间窗口，不要求三次读取产生三帧。
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        frame = http.request("GET", "/api/v1/frame?display=none")
        if frame["available"] and frame["current"] and int(frame["frameSeq"]) > baseline:
            return
        time.sleep(0.02)
    raise AssertionError("大帧请求及慢客户端存在时，连续采集未推进")


def exercise(http, scpi_port, token):
    # 先确认采集就绪，再把大帧、轻量状态与只读 SCPI 放进同一网络执行器。
    deadline = time.monotonic() + 10
    while True:
        frame = http.frame()
        if frame["available"] and frame["current"]:
            validate_frame(frame)
            break
        assert time.monotonic() < deadline, "等待第一个完整采集帧超时"
        time.sleep(0.01)
    scpi = connect_scpi(scpi_port)
    observer = HttpClient(http.port)
    try:
        barrier = threading.Barrier(3)
        with ThreadPoolExecutor(max_workers=3) as pool:
            frame_samples = pool.submit(measure_frames, http, barrier)
            status_samples = pool.submit(measure_status, observer, token, barrier)
            control_samples = pool.submit(measure_scpi, scpi, barrier)
            measured = {
                "frame": frame_samples.result(),
                "status": status_samples.result(),
                "scpi": control_samples.result(),
            }
        medians = {name: statistics.median(values) for name, values in measured.items()}
        maxima = {name: max(values) for name, values in measured.items()}
        metrics = {name + "MedianMs": round(value * 1000, 2) for name, value in medians.items()}
        metrics.update({name + "MaxMs": round(value * 1000, 2) for name, value in maxima.items()})
        print(json.dumps(metrics))
        # 阈值给 Debug 构建及共享 CI 留出余量，旧编码节拍稳定超过两秒。
        assert medians["frame"] < FRAME_MEDIAN_LIMIT, "4000 点四迹线仍被分片之间的人为等待拖慢"
        assert medians["status"] < CONTROL_MEDIAN_LIMIT, "大帧编码阻塞了状态查询"
        assert medians["scpi"] < CONTROL_MEDIAN_LIMIT, "大帧编码阻塞了 SCPI 查询"
        assert maxima["status"] < 1.0 and maxima["scpi"] < 1.0, "不能用中位数掩盖控制查询的长阻塞"
        # 不读取大响应，制造网络背压；其余客户端仍须正常读回。
        with socket.create_connection(("127.0.0.1", http.port), timeout=5) as slow:
            slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            slow.sendall(b"GET /api/v1/frame HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            time.sleep(0.2)
            assert statistics.median(measure_status(http, token)) < CONTROL_MEDIAN_LIMIT
            assert scpi.query("*IDN?").startswith("VnaPro,")
        check_acquisition_progress(http, int(frame["frameSeq"]))
    finally:
        observer.close()
        scpi.close()


def main():
    assert len(sys.argv) == 3, "usage: WebFrameSchedulingTest.py <server> <tdc1vn10-driver>"
    environment = dict(os.environ, VNAPRO_SIM_MODE="noise", VNAPRO_SIM_SEED="42")
    with tempfile.TemporaryDirectory(prefix="vnapro-frame-scheduling-") as directory:
        config = Path(directory) / "vnapro.yml"
        config.write_text("log:\n  file:\n    enabled: false\n", encoding="utf-8")
        with running_server(sys.argv[1], sys.argv[2], environment, config=config, board="tdc1vn10") as ports:
            http = HttpClient(ports["http"])
            exercise(http, ports["scpi"], configure(http))
    print("PASS 4000 点四迹线实时响应、控制查询公平性与慢客户端隔离")


if __name__ == "__main__":
    main()
