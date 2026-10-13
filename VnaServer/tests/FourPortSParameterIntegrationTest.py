"""通过现有 VN08/VN10 驱动验收四端口十六参数、同源参考及 HTTP/SCPI 一致性。"""

import os
from pathlib import Path
import sys
import time

from GoldenIqIntegrationTest import golden_vectors, running_server
from SParameterIntegrationTest import compare_complex, read_complex, trace_complex
from WebHttpIntegrationTest import HttpClient, connect_scpi, wait_no_owner, wait_operation


PARAMETERS = [(f"Matrix{receiver}{source}", f"S{receiver}{source}", receiver, source)
              for receiver in range(1, 5) for source in range(1, 5)]


def command(client, text):
    assert int(client.query(text + ";*OPC?")) == 1, text
    assert client.query("SYST:ERR?").startswith("0,"), text


def define_matrix(client):
    for name, parameter, receiver, source in PARAMETERS:
        # 单位数端口的下划线别名是 Keysight 标准语法，目录输出保持规范 Sij。
        argument = "S3_4" if parameter == "S34" else parameter
        command(client, f'CALC1:PAR:DEF:EXT "{name}","{argument}"')
    for source in range(1, 5):
        command(client, f'CALC1:PAR:DEF:EXT "Reference{source}","a{source},{source}"')
        for receiver in range(1, 5):
            command(client, f'CALC1:PAR:DEF:EXT "Wave{receiver}{source}","b{receiver},{source}"')


def held_matrix(http, client, vectors=None):
    frame = http.frame([name for name, _, _, _ in PARAMETERS])
    status = http.status()
    assert frame["available"] and frame["current"], frame
    assert frame["revision"] == status["acquisitionRevision"]
    assert frame["measurementRevision"] == status["revision"]
    assert len(frame["traces"]) == 16 and not frame["decimated"]
    assert [trace["name"] for trace in frame["traces"]] == [item[0] for item in PARAMETERS]
    for trace, (name, parameter, receiver, source) in zip(frame["traces"], PARAMETERS):
        assert (trace["parameter"], trace["receiverPort"], trace["sourcePort"]) == (parameter, receiver, source)
        assert trace["kind"] == "sParameter" and trace["unit"] == "1"
        assert trace["calibration"] == "uncalibrated"
        reference = read_complex(client, f"Reference{source}")
        numerator = read_complex(client, f"Wave{receiver}{source}")
        expected = [b / a for a, b in zip(reference, numerator)]
        if vectors is not None:
            # 预期值来自独立固定 IQ 字面量，不导入正式信号模型或测量公式。
            point = vectors[source - 1][0]
            a_offset = (source - 1) * 4
            b_offset = (receiver - 1) * 4 + 2
            expected = [complex(*point[b_offset:b_offset + 2]) /
                        complex(*point[a_offset:a_offset + 2])] * 2
        compare_complex(trace_complex(trace), expected, parameter + " HTTP ratio")
        compare_complex(read_complex(client, name), expected, parameter + " SCPI ratio")
    after = http.frame([name for name, _, _, _ in PARAMETERS])
    assert all(after[key] == frame[key] for key in ("revision", "runId", "frameSeq")), \
        "读取十六参数不能更换完整帧或重新触发采集"
    return frame


def continuous_matrix(http, client):
    command(client, "SENS1:SWE:MODE CONT")
    deadline = time.monotonic() + 8
    sequences, changed, previous = set(), set(), {}
    while len(sequences) < 12:
        assert time.monotonic() < deadline, "四激励源完整轮次没有持续更新"
        frame = http.frame([name for name, _, _, _ in PARAMETERS])
        if not frame["available"] or not frame["current"]:
            time.sleep(0.002)
            continue
        assert len(frame["traces"]) == 16
        for trace in frame["traces"]:
            values = trace_complex(trace)
            if trace["name"] in previous and previous[trace["name"]] != values:
                changed.add(trace["name"])
            previous[trace["name"]] = values
        sequences.add(frame["frameSeq"])
    assert changed == {name for name, _, _, _ in PARAMETERS}, "十六项噪声测量必须全部更新"
    command(client, "SENS1:SWE:MODE HOLD")
    held_matrix(http, client)


def live_http_selection(http):
    wait_no_owner(http)
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    try:
        accepted = http.request("POST", "/api/v1/operations", {
            "action": "continuous", "requestId": "matrix-continuous",
        }, token, expected=202)
        wait_operation(http, accepted["operationId"], token)
        before = http.status(token)
        # 以 UI 同样的标准参数文本原位修改活动迹线，已采到的四源 IQ 可直接复算。
        body = {"parameter": "S44", "expectedRevision": before["revision"],
                "requestId": "matrix-http-S44"}
        accepted = http.request("PUT", "/api/v1/measurements/Trc1", body, token, expected=202)
        wait_operation(http, accepted["operationId"], token)
        after = http.status(token)
        selected = next(item for item in after["measurements"] if item["name"] == "Trc1")
        assert selected["parameter"] == "S44" and selected["receiverPort"] == selected["sourcePort"] == 4
        assert after["config"]["sweepMode"] == "CONT" and after["runId"] == before["runId"]
        assert after["acquisitionRevision"] == before["acquisitionRevision"], \
            "已覆盖的四端口测量选择不能重新下发采集计划"
        deadline = time.monotonic() + 5
        while True:
            frame = http.frame(["Trc1", "Matrix44"])
            if frame["available"] and frame["current"] and frame["measurementRevision"] == after["revision"]:
                break
            assert time.monotonic() < deadline, "S44 更新没有读回匹配目录版本的完整帧"
        compare_complex(trace_complex(frame["traces"][0]), trace_complex(frame["traces"][1]),
                        "同帧 HTTP S44 原位选择")
        http.request("PUT", "/api/v1/measurements/Trc1",
                     dict(body, requestId="matrix-http-stale"), token, expected=409)
        assert http.status(token)["measurements"] == after["measurements"], "旧版本候选不得覆盖 S44"
        return frame["frameSeq"]
    finally:
        http.request("POST", "/api/v1/control/release", {}, token)


def exercise(server, driver, board, mode, vectors):
    environment = dict(os.environ, VNAPRO_SIM_MODE=mode, VNAPRO_SIM_SEED="42")
    config = Path(__file__).with_name("golden_test.yml")
    with running_server(server, driver, environment, config=config, board=board) as ports:
        http = HttpClient(ports["http"])
        assert http.status()["capabilities"]["portCount"] == 4
        client = connect_scpi(ports["scpi"])
        try:
            command(client, "SENS1:SWE:MODE HOLD")
            if mode == "golden":
                command(client, "SENS1:FREQ:STOP 1GHz;STAR 1GHz;:SENS1:SWE:POIN 2")
            else:
                command(client, "SENS1:SWE:POIN 17")
            define_matrix(client)
            command(client, "SENS1:SWE:MODE SING")
            # VN10 使用独立信号模型；旧 VN08 黄金字面量只校验 VN08。
            # 两种驱动仍都以同源原始 a/b 波和独立 Python 复数除法校验全部 16 项。
            golden = vectors if mode == "golden" and board == "tdc1vn08" else None
            held_matrix(http, client, golden)
            if mode == "noise":
                continuous_matrix(http, client)
        finally:
            client.close()
        live_http_selection(http)
        wait_no_owner(http)
        client = connect_scpi(ports["scpi"])
        try:
            command(client, 'CALC1:PAR:SEL "Trc1";:SENS1:SWE:MODE HOLD')
            assert "Trc1,S44" in client.query("CALC1:PAR:CAT:EXT?")
            frame = http.frame(["Trc1"])
            compare_complex(read_complex(client, "Trc1"), trace_complex(frame["traces"][0]),
                            "HTTP 选择 S44 后 SCPI 共享读回")
        finally:
            client.close()


def main():
    assert len(sys.argv) == 5, "usage: FourPortSParameterIntegrationTest.py <server> <VN08> <VN10> <vectors>"
    vectors = golden_vectors(sys.argv[4])
    for board, driver in (("tdc1vn08", sys.argv[2]), ("tdc1vn10", sys.argv[3])):
        for mode in ("golden", "noise"):
            exercise(sys.argv[1], driver, board, mode, vectors)
    print("四端口十六参数验收通过：原始 VN08/VN10 驱动、黄金/噪声比值、CONT 与 HTTP/SCPI 共享状态")


if __name__ == "__main__":
    main()
