"""实际进程验收 204 个四端口校准集的容量、同帧校正、重启恢复及落盘失败。

所有系数均为显式软件夹具，不代表实机标准测量或硬件校准精度。
"""

import csv
import json
import math
import os
import queue
import subprocess
import sys
import threading
import time
from collections import deque
from contextlib import contextmanager
from pathlib import Path

from CalibrationIntegrationTest import action
from server_startup import isolated_server, listening_port
from VnbScpiIntegrationTest import (
    PARAMETERS,
    SELECTORS,
    Client,
    command,
    define_matrix,
    prepare_sweep,
    select_ascii,
)
from WebHttpIntegrationTest import HttpClient, wait_no_owner

COUNT = 204
POINTS = 800
STORE_DIRECTORY = "calibration"
MANIFEST_NAME = "channel-1.json"


def set_name(index):
    return f"CableTest software pool {index + 1:03d} / four-port TOSM"


def coefficients(set_index, point, cell):
    """各集、频点和矩阵格均可区分；非零匹配只置于对角，便于独立核算。"""
    offset = complex(
        (set_index + 1) * 0.00001 + (cell + 1) * 0.0003 + point * 0.0000007,
        -(cell + 1) * 0.0002 - point * 0.0000003,
    )
    tracking = complex(
        1.3 + set_index * 0.001 + cell * 0.013 + point * 0.00001,
        0.02 + cell * 0.0004 + set_index * 0.00001 + point * 0.0000001,
    )
    match = (
        complex(0.01 + set_index * 0.0001, -0.002 + point * 0.00000001)
        if cell % 5 == 0
        else 0j
    )
    return offset, match, tracking


def document(identity, axis, index):
    rows = []
    for point in range(len(axis)):
        row = []
        for cell in range(16):
            for value in coefficients(index, point, cell):
                row.extend((value.real, value.imag))
        rows.append(row)
    return {
        "schema": 3,
        "id": "0",
        "name": set_name(index),
        "deviceIdentity": identity,
        "method": "fourPort",
        "port1": 1,
        "port2": 4,
        "includeIsolation": True,
        "ifBandwidthHz": 1000,
        "frequencyHz": axis,
        "powerDbm": [0.0] * len(axis),
        "onePortTerms": [],
        "twoPortTerms": [],
        "fourPortTerms": rows,
    }


@contextmanager
def running(bundle):
    """同一程序目录可反复启动；仅保留有限启动日志，等待实际随机监听端口。"""
    process = subprocess.Popen(
        [bundle.executable],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        env=bundle.environment,
    )
    tail, events = deque(maxlen=32), queue.Queue()

    def collect():
        for line in process.stdout:
            tail.append(line.rstrip())
            listener = listening_port(line)
            if listener is not None:
                events.put(listener)
        events.put(None)

    reader = threading.Thread(target=collect, daemon=True)
    reader.start()
    started = time.monotonic()
    # 205×800 点未优化 Debug 恢复曾实测 84 秒，启动单独留有界余量。
    ports, deadline = {}, started + 120
    try:
        while len(ports) != 2:
            listener = events.get(timeout=max(0.01, deadline - time.monotonic()))
            assert listener is not None, "持久化服务在监听前退出：" + repr(list(tail))
            ports[listener[0]] = listener[1]
            assert time.monotonic() < deadline, "恢复校准集后服务未就绪"
        print(
            f"Persistence server ready in {time.monotonic() - started:.2f}s", flush=True
        )
        yield ports
        assert process.poll() is None, "服务在验收期间异常退出：" + repr(list(tail))
    except Exception:
        print(
            f"Persistence server process: pid={process.pid}, exit={process.poll()}",
            flush=True,
        )
        print("Persistence server tail:", list(tail), flush=True)
        raise
    finally:
        if process.poll() is None:
            # Windows terminate 不执行进程退出保存；成功变更必须此前已落盘。
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        reader.join()
        process.stdout.close()


def mutate(http, token, kind, request_id, succeed=True, history_stats=None, **fields):
    # 请求体含 revision 和 requestId；背压重试必须复用同一正文，不能重新取版本。
    body = action(http, kind, request_id, **fields)
    renewed = time.monotonic()
    deadline = renewed + 120
    while True:
        assert time.monotonic() < deadline, (
            "retry history backpressure timeout: " + request_id
        )
        if time.monotonic() - renewed >= 5:
            renew(http, token)
            renewed = time.monotonic()
        response = write_request(
            http,
            "POST",
            "/api/v1/calibration",
            request_id,
            body,
            token,
            expected=(202, 429),
        )
        if "error" not in response:
            break
        assert response["error"]["code"] in ("retry_history_full", "operation_limit"), (
            response
        )
        if history_stats is not None:
            if history_stats["retries"] == 0:
                print(
                    "Bounded HTTP operation history is full; waiting with the same request body",
                    flush=True,
                )
            history_stats["retries"] += 1
        time.sleep(1)
    while True:
        if time.monotonic() - renewed >= 5:
            renew(http, token)
            renewed = time.monotonic()
        result = write_request(
            http,
            "GET",
            "/api/v1/operations/" + response["operationId"],
            request_id,
            token=token,
        )
        if result["state"] != "pending":
            assert result["state"] == ("completed" if succeed else "failed"), result
            assert result["result"]["ok"] is succeed, result
            if not succeed:
                assert (
                    result["result"]["errorCode"] != 0 and result["result"]["message"]
                ), result
            return result
        assert time.monotonic() < deadline, result
        # 串行复用连接，保留原有大池验收的 50 ms 轮询节拍。
        time.sleep(0.05)


def write_request(http, method, path, request_id, body=None, token=None, expected=200):
    started = time.monotonic()
    try:
        return http.request(method, path, body, token, expected=expected)
    except Exception as failure:
        raise AssertionError(
            f"{request_id}: {method} {path} failed after "
            f"{time.monotonic() - started:.2f}s: {type(failure).__name__}: {failure}"
        ) from failure


def renew(http, token):
    claim = http.request("POST", "/api/v1/control/claim", {"sessionId": token}, token)
    assert claim["sessionId"] == token and claim["hasControl"], claim


def pool(http):
    return [
        (entry["id"], entry["name"]) for entry in http.status()["calibration"]["sets"]
    ]


def catalog(client):
    entries = next(csv.reader([client.query("SENS1:CORR:CSET:CAT?")]))
    if entries == [""]:
        return []
    assert len(entries) % 2 == 0, entries
    return list(zip(entries[::2], entries[1::2]))


def prepare(client, http, points):
    prepare_sweep(client, points)
    define_matrix(client)
    command(client, "INIT1:IMM")
    raw = [select_ascii(client, name, "RDATA") for name, _ in PARAMETERS]
    frame = http.frame([name for name, _ in PARAMETERS])
    assert frame["available"] and frame["current"] and len(frame["traces"]) == 16
    assert len(frame["frequencyHz"]) == points and all(
        len(values) == points * 2 for values in raw
    )
    return raw, frame


def corrected(client, http, index, raw, frame, set_id):
    command(client, f"SENS1:CORR:CSET:ACT '{set_name(index)}',0")
    assert client.query("SENS1:CORR:CSET:ACT?") == '"' + set_id + '"'
    assert client.query("SENS1:CORR:STAT?") == "1"
    command(client, "FORM REAL,64;:FORM:BORD SWAP")
    points = len(frame["frequencyHz"])
    actual = client.block(SELECTORS, points, "<")
    expected = [[] for _ in range(16)]
    for point in range(points):
        normalized, diagonal = [], []
        for cell in range(16):
            offset, match, tracking = coefficients(index, point, cell)
            value = (complex(*raw[cell][point * 2 : point * 2 + 2]) - offset) / tracking
            normalized.append(value)
            if cell % 5 == 0:
                diagonal.append(1.0 + match * value)
        # B 为对角矩阵，S=A*inverse(B) 按列除以对应对角值。
        for cell, value in enumerate(normalized):
            value /= diagonal[cell % 4]
            expected[cell].extend((value.real, value.imag))
    wanted = [value for trace in expected for value in trace]
    assert len(actual) == len(wanted)
    assert all(
        math.isfinite(value) and abs(value - reference) < 1e-11
        for value, reference in zip(actual, wanted)
    ), "四口系数未作用于完整 16S 数据"
    after = http.frame([name for name, _ in PARAMETERS])
    assert (
        after["current"]
        and after["runId"] == frame["runId"]
        and after["frameSeq"] == frame["frameSeq"]
    ), "切换已兼容校准集不能替换原始完整帧"
    return actual


def check_export(http, identity, axis, index, set_id):
    exported = http.request("GET", "/api/v1/calibration/sets/" + set_id)
    expected = document(identity, axis, index)
    expected["id"] = set_id
    assert exported == expected, "重启前后导出的 800×96 系数必须逐值相同"


def import_pool(http, identity, axis):
    wait_no_owner(http)
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    started = time.monotonic()
    renewed = started
    history_stats = {"retries": 0}
    try:
        # 顺序产生和发送每个文档，禁止在测试进程持有 204 份大系数副本。
        for index in range(COUNT):
            if index < 3:
                print(f"Submitting pool-import-{index} ({POINTS} points)", flush=True)
            if time.monotonic() - renewed >= 5:
                renew(http, token)
                renewed = time.monotonic()
            mutate(
                http,
                token,
                "import",
                "pool-import-" + str(index),
                history_stats=history_stats,
                data=document(identity, axis, index),
            )
            if (index + 1) % 32 == 0 or index + 1 == COUNT:
                print(
                    f"Imported {index + 1}/{COUNT} real names, {time.monotonic() - started:.2f}s, "
                    f"bounded-history retries={history_stats['retries']}",
                    flush=True,
                )
        result = pool(http)
        assert len(result) == COUNT and len({entry[0] for entry in result}) == COUNT
        assert [entry[1] for entry in result] == [
            set_name(index) for index in range(COUNT)
        ]
        assert all(
            entry["points"] == POINTS and entry["method"] == "fourPort"
            for entry in http.status()["calibration"]["sets"]
        )
        return result
    finally:
        http.request("POST", "/api/v1/control/release", {}, token)


def capacity_and_restart(bundle):
    with running(bundle) as ports:
        http, client = HttpClient(ports["http"]), Client(ports["scpi"])
        try:
            raw, frame = prepare(client, http, POINTS)
            identity = http.status()["identity"]
        finally:
            client.close()
        original = import_pool(http, identity, frame["frequencyHz"])
        wait_no_owner(http)
        client = Client(ports["scpi"])
        try:
            assert catalog(client) == original
            first = corrected(client, http, 0, raw, frame, original[0][0])
            last = corrected(client, http, COUNT - 1, raw, frame, original[-1][0])
            assert first != last, "首尾夹具必须包含不同的实际非单位系数"
            for index in (0, COUNT - 1):
                check_export(
                    http, identity, frame["frequencyHz"], index, original[index][0]
                )
            command(client, "*RST")
            assert catalog(client) == original, "*RST 不得删除持久化校准池"
            raw, frame = prepare(client, http, POINTS)
            corrected(client, http, COUNT - 1, raw, frame, original[-1][0])
        finally:
            client.close()
    print("Restarting the same directory with all 204 calibration sets", flush=True)
    with running(bundle) as ports:
        http, client = HttpClient(ports["http"]), Client(ports["scpi"])
        try:
            assert pool(http) == original and catalog(client) == original
            restored = http.status()["calibration"]
            assert (
                not restored["enabled"] and restored["selectedSetId"] == original[-1][0]
            )
            assert client.query("SENS1:CORR:STAT?") == "0"
            raw, frame = prepare(client, http, POINTS)
            for index in (0, COUNT - 1):
                check_export(
                    http, identity, frame["frequencyHz"], index, original[index][0]
                )
                corrected(client, http, index, raw, frame, original[index][0])
            command(client, f"SENS1:CORR:CSET:DEL '{original[0][0]}'")
            assert catalog(client) == original[1:]
        finally:
            client.close()
    with running(bundle) as ports:
        http, client = HttpClient(ports["http"]), Client(ports["scpi"])
        try:
            assert pool(http) == original[1:] and catalog(client) == original[1:], (
                "删除必须跨重启保留且不重排 ID"
            )
            assert not http.status()["calibration"]["enabled"]
        finally:
            client.close()
    print(
        "204-name/800-point persistence passed: IDs, coefficients, 16S correction, reset and durable delete",
        flush=True,
    )


def reject_corrupt_startup(bundle):
    result = subprocess.run(
        [bundle.executable],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        env=bundle.environment,
        timeout=25,
        check=False,
    )
    assert result.returncode != 0, "损坏的校准存储不能当作空池成功启动"
    assert not any(
        listening_port(line) is not None for line in result.stdout.splitlines()
    ), result.stdout[-4000:]


def fault_cases(bundle):
    with running(bundle) as ports:
        http, client = HttpClient(ports["http"]), Client(ports["scpi"])
        try:
            raw, frame = prepare(client, http, 3)
            identity = http.status()["identity"]
        finally:
            client.close()
        wait_no_owner(http)
        token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
        try:
            mutate(
                http,
                token,
                "import",
                "fault-original",
                data=document(identity, frame["frequencyHz"], 0),
            )
        finally:
            http.request("POST", "/api/v1/control/release", {}, token)
        wait_no_owner(http)
        client = Client(ports["scpi"])
        try:
            original = catalog(client)
            corrected(client, http, 0, raw, frame, original[0][0])
        finally:
            client.close()
        wait_no_owner(http)
        base = bundle.executable.parent.resolve()
        store = (base / STORE_DIRECTORY).resolve()
        backup = base / "calibration-save-failure-backup"
        # 所有移动都严格限定于本次独立测试目录，保留原始文件作恢复依据。
        assert store.parent == backup.parent == base and not store.is_symlink()
        manifest_bytes = (store / MANIFEST_NAME).read_bytes()
        store.rename(backup)
        store.write_text("force a real filesystem save failure", encoding="utf-8")
        token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
        before = http.status()["calibration"]
        try:
            mutate(
                http,
                token,
                "import",
                "fault-failed-import",
                succeed=False,
                data=document(identity, frame["frequencyHz"], 1),
            )
            mutate(
                http,
                token,
                "delete",
                "fault-failed-delete",
                succeed=False,
                setId=original[0][0],
            )
            after = http.status()["calibration"]
            assert pool(http) == original
            for field in ("selectedSetId", "enabled", "compatible"):
                assert after[field] == before[field], field
            assert (backup / MANIFEST_NAME).read_bytes() == manifest_bytes, (
                "保存失败不得改坏旧 manifest"
            )
        finally:
            http.request("POST", "/api/v1/control/release", {}, token)
            store.unlink()
            backup.rename(store)
        wait_no_owner(http)
        client = Client(ports["scpi"])
        try:
            command(client, "*CLS")
            corrected(client, http, 0, raw, frame, original[0][0])
        finally:
            client.close()
    with running(bundle) as ports:
        http, client = HttpClient(ports["http"]), Client(ports["scpi"])
        try:
            assert catalog(client) == original, "失败的导入或删除不得被恢复成已提交状态"
            raw, frame = prepare(client, http, 3)
            corrected(client, http, 0, raw, frame, original[0][0])
        finally:
            client.close()
    manifest = bundle.executable.parent / STORE_DIRECTORY / MANIFEST_NAME
    manifest.write_text("{broken manifest", encoding="utf-8")
    reject_corrupt_startup(bundle)
    manifest.write_bytes(manifest_bytes)
    reference = json.loads(manifest_bytes)["sets"][0]["file"]
    payload = (manifest.parent / reference).resolve()
    assert payload.parent == manifest.parent.resolve(), "存储引用必须位于当前测试池"
    payload.write_text('{"schema":3,"tampered":true}', encoding="utf-8")
    reject_corrupt_startup(bundle)
    print(
        "Persistence fault paths passed: real save failure rollback and corrupt store startup rejection",
        flush=True,
    )


def main():
    assert len(sys.argv) in (3, 4), (
        "usage: CalibrationPersistenceIntegrationTest.py <server> <VN10 simulator> [--faults-only]"
    )
    assert len(sys.argv) != 4 or sys.argv[3] == "--faults-only"
    environment = dict(os.environ, VNAPRO_SIM_MODE="golden", VNAPRO_SIM_SEED="42")
    config = Path(__file__).with_name("golden_test.yml")
    started = time.monotonic()
    if len(sys.argv) == 3:
        with isolated_server(
            sys.argv[1], sys.argv[2], environment, config=config, board="tdc1vn10"
        ) as bundle:
            capacity_and_restart(bundle)
    with isolated_server(
        sys.argv[1], sys.argv[2], environment, config=config, board="tdc1vn10"
    ) as bundle:
        fault_cases(bundle)
    print(
        f"Calibration persistence integration passed in {time.monotonic() - started:.2f}s",
        flush=True,
    )


if __name__ == "__main__":
    main()
