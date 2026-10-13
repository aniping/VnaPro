"""验收确认后整组替换、扫描设置保留及原始双板驱动的完整矩阵链路。"""

import os
from pathlib import Path
import sys
import tempfile
import time

from GoldenIqIntegrationTest import running_server
from SParameterIntegrationTest import compare_complex, read_complex, trace_complex
from WebHttpIntegrationTest import HttpClient, accepted, connect_scpi, wait_no_owner, wait_operation


ACTION = "/api/v1/measurement-actions/all-sparameters"
APPLIED_KEYS = ("revision", "acquisitionRevision", "runId", "config", "measurements", "display")


def scene(status):
    """连续扫描允许帧号推进，拒绝请求不得改变已应用仪器状态。"""
    return {key: status[key] for key in APPLIED_KEYS}


def command(client, text):
    assert int(client.query(text + ";*OPC?")) == 1, text
    assert client.query("SYST:ERR?").startswith("0,"), text


def display_change(http, token, action, request_id, **fields):
    body = dict(fields, action=action, requestId=request_id,
                expectedDisplayRevision=http.status()["display"]["revision"])
    wait_operation(http, accepted(http, "display", body, token), token)


def prepare_scene(http, token):
    status = http.status()
    wait_operation(http, accepted(http, "measurements", {
        "name": "OldRaw", "parameter": "a1,1", "expectedRevision": status["revision"],
        "requestId": "old-raw-definition",
    }, token), token)
    display_change(http, token, "select", "old-raw-feed", traceName="OldRaw", diagram=1)
    display_change(http, token, "format", "old-format", traceName="Trc1", format="phase")
    display_change(http, token, "scale", "old-scale", traceName="Trc1", min=-11, max=29)
    for index, name in enumerate(("Trc1", "OldRaw"), start=1):
        current = http.status()
        display_change(http, token, "marker", "old-marker-" + name, traceName=name,
                       markerId=index, enabled=True, frequencyHz=current["config"]["startHz"],
                       acquisitionRevision=current["acquisitionRevision"])
    display_change(http, token, "infoTable", "old-info-table", enabled=True, position="left")
    display_change(http, token, "displayAnnotations", "old-annotations", activeTraceOnly=True,
                   stimulusInfoVisible=False, fontScale=125)
    before = http.status(token)
    assert before["config"]["sweepMode"] == "CONT" and before["state"] == "Running"
    assert before["display"]["selectedMeasurement"] == "OldRaw"
    assert before["display"]["presentations"]["Trc1"]["format"] == "phase"
    assert any(marker["enabled"] for marker in before["display"]["presentations"]["OldRaw"]["markers"])
    return before


def request_body(status, request_id):
    return {"requestId": request_id, "expectedRevision": status["revision"],
            "expectedDisplayRevision": status["display"]["revision"], "confirmed": True}


def wait_rejected(http, response, token):
    if "error" in response:
        assert response["error"]["code"] in (
            "revision_conflict", "display_revision_conflict", "operation_rejected",
        ), response
        return
    deadline = time.monotonic() + 8
    while True:
        result = http.request("GET", "/api/v1/operations/" + response["operationId"], token=token)
        if result["state"] != "pending":
            assert result["state"] == "failed" and not result["result"]["ok"], result
            assert "revision" in result["result"]["message"].lower(), result
            return
        assert time.monotonic() < deadline, result
        time.sleep(0.002)


def rejected_candidates(http, token, before):
    original = scene(before)
    bad_confirmations = ({"confirmed": False}, {"confirmed": "true"}, {"confirmed": 1},
                         {"confirmed": None}, {"diagram": 1})
    for index, override in enumerate(bad_confirmations):
        body = dict(request_body(before, "bad-confirmation-" + str(index)), **override)
        http.request("POST", ACTION, body, token, expected=400)
        assert scene(http.status(token)) == original, "错误确认不能清除旧定义、尺度或 Marker"
    missing = request_body(before, "missing-confirmation")
    del missing["confirmed"]
    http.request("POST", ACTION, missing, token, expected=400)
    assert scene(http.status(token)) == original
    for key in ("expectedRevision", "expectedDisplayRevision"):
        body = request_body(before, "stale-" + key)
        body[key] = str(int(body[key]) - 1)
        response = http.request("POST", ACTION, body, token, expected=(202, 409))
        wait_rejected(http, response, token)
        assert scene(http.status(token)) == original, "过期候选不得部分替换测量或显示"


def matrix(port_count):
    return [(f"Trc{(receiver - 1) * port_count + source}", f"S{receiver}{source}", receiver, source)
            for receiver in range(1, port_count + 1) for source in range(1, port_count + 1)]


def assert_matrix(status, before, port_count):
    expected = matrix(port_count)
    names = [item[0] for item in expected]
    assert len(status["measurements"]) == port_count ** 2
    for definition, (name, parameter, receiver, source) in zip(status["measurements"], expected):
        assert (definition["name"], definition["parameter"], definition["receiverPort"],
                definition["sourcePort"], definition["kind"]) == (name, parameter, receiver, source, "sParameter")
    display = status["display"]
    assert display["layout"] == "grid" and display["gridColumns"] == display["gridRows"] == port_count
    assert display["selectedDiagram"] == 1 and display["selectedMeasurement"] == "Trc1"
    assert display["displayedNames"] == names and set(display["presentations"]) == set(names)
    assert len(display["diagrams"]) == port_count ** 2
    for index, diagram in enumerate(display["diagrams"], start=1):
        assert diagram["number"] == index and diagram["measurementNames"] == [names[index - 1]]
        assert diagram["primaryMeasurement"] == names[index - 1] and diagram["traceSlots"] == [1]
    for presentation in display["presentations"].values():
        assert presentation["format"] == "db" and presentation["visible"]
        assert presentation["scale"] == {"min": -90, "max": 10, "referencePosition": 9}
        assert not presentation["marker"]["enabled"] and not any(
            marker["enabled"] for marker in presentation["markers"]
        ), "同名新 Trc1 也不能继承旧 Marker"
    assert display["infoTable"] == before["display"]["infoTable"]
    assert display["annotations"] == before["display"]["annotations"]
    assert status["config"] == before["config"] and status["runId"] == before["runId"]
    assert status["state"] == "Running", "整组创建不能退回 HOLD"
    return names


def observer_catalog(http, port, token, expected):
    client = connect_scpi(port)
    try:
        flattened = [part for name, parameter, _, _ in expected for part in (name, parameter)]
        assert client.query("CALC1:PAR:CAT:EXT?").strip('"').split(",") == flattened
        assert client.query("CALC1:PAR:SEL?") == '"Trc1"'
        assert client.query("CALC1:MARK1:STAT?") == "0"
        for index in range(1, len(expected) + 1):
            assert client.query(f"DISP:WIND{index}:STAT?") == "1"
            assert client.query(f"DISP:WIND{index}:CAT?") == '"1"'
            assert client.query(f"DISP:WIND{index}:TRAC1:STAT?") == "1"
        assert http.status(token)["control"]["owner"] == "web", "协议观察不能抢写控制权"
    finally:
        client.close()


def continuous_frames(http, status, names, mode):
    deadline = time.monotonic() + 8
    sequences, changed, previous = set(), set(), {}
    while len(sequences) < 8:
        assert time.monotonic() < deadline, "矩阵完整帧没有持续推进"
        frame = http.frame(names)
        if not frame["available"] or not frame["current"] or frame["measurementRevision"] != status["revision"]:
            time.sleep(0.002)
            continue
        assert frame["revision"] == status["acquisitionRevision"] and frame["runId"] == status["runId"]
        assert len(frame["traces"]) == len(names) and not frame["decimated"]
        assert frame["originalPointCount"] == frame["displayPointCount"] == status["config"]["points"]
        assert frame["frequencyHz"][0] == status["config"]["startHz"]
        assert frame["frequencyHz"][-1] == status["config"]["stopHz"]
        for trace in frame["traces"]:
            values = trace_complex(trace)
            if trace["name"] in previous and previous[trace["name"]] != values:
                changed.add(trace["name"])
            previous[trace["name"]] = values
        sequences.add(frame["frameSeq"])
    if mode == "noise":
        assert changed == set(names), "噪声模式全部矩阵成员应随完整帧更新"


def independent_ratios(http, port, port_count):
    wait_no_owner(http)
    client = connect_scpi(port)
    try:
        command(client, "*CLS;:SENS1:SWE:MODE HOLD")
        held = http.status()
        expected = matrix(port_count)
        for source in range(1, port_count + 1):
            command(client, f'CALC1:PAR:DEF:EXT "RawRef{source}","a{source},{source}"')
            for receiver in range(1, port_count + 1):
                command(client, f'CALC1:PAR:DEF:EXT "RawB{receiver}{source}","b{receiver},{source}"')
        assert http.status()["acquisitionRevision"] == held["acquisitionRevision"], \
            "完整矩阵计划已覆盖原始波，追加校验定义不得重新采集"
        frame = http.frame([item[0] for item in expected])
        assert frame["available"] and frame["current"]
        assert frame["runId"] == held["runId"]
        for trace, (name, parameter, receiver, source) in zip(frame["traces"], expected):
            reference = read_complex(client, f"RawRef{source}")
            numerator = read_complex(client, f"RawB{receiver}{source}")
            # 从同一 HOLD 帧的原始波用 Python 复数除法计算，不调用正式测量或信号模型。
            ratio = [b / a for a, b in zip(reference, numerator)]
            compare_complex(trace_complex(trace), ratio, parameter + " 原始波/HTTP 比值")
            compare_complex(read_complex(client, name), ratio, parameter + " 原始波/SCPI 比值")
            command(client, f"DISP:WIND{(receiver - 1) * port_count + source}:TRAC1:SEL")
            assert client.query("CALC1:PAR:SEL?") == '"' + name + '"', "图窗槽必须选择本矩阵成员"
        later = http.frame([item[0] for item in expected])
        assert all(later[key] == frame[key] for key in ("revision", "runId", "frameSeq")), \
            "读数与选择不得更换或拼接完整源帧"
    finally:
        client.close()


def exercise(server, driver, board, port_count, mode, directory):
    config = Path(directory) / f"{board}-{port_count}-{mode}.yml"
    config.write_text(
        f"instrument:\n  portCount: {port_count}\n  sweep:\n"
        "    defaultStartHz: 20000000\n    defaultStopHz: 600000000\n    defaultPoints: 17\n"
        "    defaultIfBandwidthHz: 10000\n    defaultSweepMode: CONT\n"
        "log: {file: {enabled: false}}\n", encoding="utf-8",
    )
    environment = dict(os.environ, VNAPRO_SIM_MODE=mode, VNAPRO_SIM_SEED="42")
    with running_server(server, driver, environment, config=config, board=board) as ports:
        http = HttpClient(ports["http"])
        observer = HttpClient(ports["http"])
        token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
        try:
            before = prepare_scene(http, token)
            rejected_candidates(http, token, before)
            response = http.request("POST", ACTION, request_body(before, "confirm-all"), token, expected=202)
            wait_operation(http, response["operationId"], token)
            after = http.status(token)
            names = assert_matrix(after, before, port_count)
            assert int(after["revision"]) == int(before["revision"]) + 1, "整组应一次更新逻辑目录"
            assert int(after["acquisitionRevision"]) == int(before["acquisitionRevision"]) + 1, \
                "原先只扫描源一，整组需求应一次更新采集计划"
            assert scene(observer.status()) == scene(after), "独立观察窗口必须读取完整已提交矩阵"
            observer_catalog(http, ports["scpi"], token, matrix(port_count))
            continuous_frames(http, after, names, mode)
            repeat = http.request("POST", ACTION, request_body(after, "repeat-all"), token, expected=202)
            wait_operation(http, repeat["operationId"], token)
            repeated = http.status(token)
            assert_matrix(repeated, before, port_count)
            assert repeated["acquisitionRevision"] == after["acquisitionRevision"], "重复整组不增加采集需求"
            assert scene(observer.status()) == scene(repeated), "刷新观察者不能读到半组定义"
        finally:
            http.request("POST", "/api/v1/control/release", {}, token)
        independent_ratios(http, ports["scpi"], port_count)
    print(f"All S-Params 通过：{board} / {port_count} 端口 / {mode}")


def main():
    assert len(sys.argv) == 4, "usage: AllSParametersIntegrationTest.py <server> <VN08> <VN10>"
    with tempfile.TemporaryDirectory(prefix="vnapro-all-sparameters-") as directory:
        for board, driver in (("tdc1vn08", sys.argv[2]), ("tdc1vn10", sys.argv[3])):
            for port_count in (2, 4):
                for mode in ("golden", "noise"):
                    exercise(sys.argv[1], driver, board, port_count, mode, directory)


if __name__ == "__main__":
    main()
