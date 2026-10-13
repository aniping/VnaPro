"""用独立 IQ 字面量和 Python 复数运算验收未校准双端口四参数的完整链路。"""

import math
import os
from pathlib import Path
import sys
import time
from urllib.parse import quote

from GoldenIqIntegrationTest import compare, golden_vectors, running_server
from WebHttpIntegrationTest import HttpClient, connect_scpi


def read_complex(client, name):
    values = [float(item) for item in client.query(
        f'CALC1:PAR:SEL "{name}";:CALC1:DATA? RDATA').split(",")]
    assert len(values) % 2 == 0 and all(math.isfinite(item) for item in values)
    return [complex(values[index], values[index + 1]) for index in range(0, len(values), 2)]


def trace_complex(trace):
    assert trace["available"] and all(trace["valid"])
    return [complex(real, imag) for real, imag in zip(trace["real"], trace["imag"])]


def compare_complex(actual, expected, context):
    compare([part for item in actual for part in (item.real, item.imag)],
            [part for item in expected for part in (item.real, item.imag)], context)


def prepare(client, noise):
    assert int(client.query("*CLS;:SENS1:SWE:MODE HOLD;*OPC?")) == 1
    assert int(client.query('CALC1:PAR:DEL "Trc1";*OPC?')) == 1
    if noise:
        assert int(client.query("SENS1:SWE:POIN 17;*OPC?")) == 1
    else:
        assert int(client.query("SENS1:FREQ:STOP 1GHz;STAR 1GHz;:SENS1:SWE:POIN 2;*OPC?")) == 1
    # 前四条 HTTP 曲线包含两个 S 参数、同源 a1 与 b2，b1 通过同帧 SCPI 读取。
    definitions = [("Reflect", "S1_1"), ("Transmit", "S21"), ("Reference", "a1,1"),
                   ("Received", "b2,1"), ("Reflected", "b1,1")]
    for name, parameter in definitions:
        assert int(client.query(f'CALC1:PAR:DEF:EXT "{name}","{parameter}";*OPC?')) == 1
    assert 'Reflect,S11,Transmit,S21' in client.query("CALC1:PAR:CAT:EXT?")
    assert client.query("SYST:ERR?").startswith("0,"), "Definition left an error"
    assert int(client.query("FORM:DATA ASC,0;:TRIG:SOUR IMM;:SENS1:SWE:MODE SING;*OPC?")) == 1
    assert client.query("SENS1:SWE:MODE?") == "HOLD"


def held_frame(http, client, golden=None):
    frame = http.frame(("Reflect", "Transmit", "Reference", "Received"))
    assert frame["available"] and frame["current"]
    traces = {trace["name"]: trace for trace in frame["traces"]}
    reference = trace_complex(traces["Reference"])
    received = trace_complex(traces["Received"])
    reflected = read_complex(client, "Reflected")
    expected = {"Reflect": [b / a for a, b in zip(reference, reflected)],
                "Transmit": [b / a for a, b in zip(reference, received)]}
    if golden is not None:
        # 固定数据由独立审核的字面量给出；不调用正式模块的公式或复数除法。
        a1 = complex(*golden[0:2])
        expected = {"Reflect": [complex(*golden[2:4]) / a1] * 2,
                    "Transmit": [complex(*golden[6:8]) / a1] * 2}
        wrong_reference = complex(*golden[6:8]) / complex(*golden[4:6])
        assert abs(expected["Transmit"][0] - wrong_reference) > 1e-3
    for name, values in expected.items():
        trace = traces[name]
        assert trace["kind"] == "sParameter" and trace["unit"] == "1"
        assert trace["calibration"] == "uncalibrated"
        assert trace["parameter"] == ("S11" if name == "Reflect" else "S21")
        compare_complex(trace_complex(trace), values, name + " HTTP ratio")
        compare_complex(read_complex(client, name), values, name + " SCPI ratio")
    assert traces["Reference"]["kind"] == "receiver" and traces["Reference"]["unit"] == "a.u."
    assert traces["Reference"]["calibration"] == "raw"
    later = http.frame()
    assert all(frame[key] == later[key] for key in ("revision", "runId", "frameSeq"))
    assert client.query("SYST:ERR?").startswith("0,")
    return frame


def unsupported_and_stale(http, client):
    previous = http.status()["revision"]
    for parameter in ("S15", "S51", "s11"):
        client.query(f'CALC1:PAR:DEF:EXT "Unsupported","{parameter}";*OPC?')
        assert client.query("SYST:ERR?").startswith("-224,"), parameter
        assert http.status()["revision"] == previous
    # 失败查询不产生数据响应；用下一条独立程序消息读取标准错误队列。
    client.socket.sendall(b"CALC1:DATA? SDATA\n")
    assert client.query("SYST:ERR?").startswith("-224,"), "SDATA must not imply correction"
    client.query("SENS1:SWE:POIN 3;*OPC?")
    assert not http.frame()["current"], "Old frame cannot represent the new configuration"
    client.socket.sendall(b'CALC1:PAR:SEL "Reflect";:CALC1:DATA? RDATA\n')
    assert client.query("SYST:ERR?").startswith("-230,"), "Stale S11 must be rejected"
    client.query("SENS1:SWE:MODE SING;*OPC?")
    assert len(read_complex(client, "Reflect")) == 3


def continuous_frames(http, client):
    client.query("SENS1:SWE:MODE CONT;*OPC?")
    deadline = time.monotonic() + 8
    sequences = set()
    previous_ratio = None
    changed = False
    while len(sequences) < 12:
        frame = http.frame(("Reflect", "Transmit", "Reference", "Received"))
        assert time.monotonic() < deadline, "Continuous S-parameter frames stopped"
        if not frame["available"] or not frame["current"]:
            # CONT 完成只代表核心已启动；首轮完整帧尚未发布属于正常状态。
            time.sleep(0.002)
            continue
        traces = {trace["name"]: trace for trace in frame["traces"]}
        expected = [b / a for a, b in zip(trace_complex(traces["Reference"]), trace_complex(traces["Received"]))]
        ratio = trace_complex(traces["Transmit"])
        compare_complex(ratio, expected, "CONT S21 single-snapshot ratio")
        sequences.add(frame["frameSeq"])
        changed |= previous_ratio is not None and ratio != previous_ratio
        previous_ratio = ratio
    client.query("SENS1:SWE:MODE HOLD;*OPC?")
    assert changed, "Noisy S21 must change between complete sweeps"
    held_frame(http, client)


TWO_PORT_MEASUREMENTS = [
    ("Reflect", "S11", 1, 1), ("Transmit", "S21", 2, 1),
    ("ReverseTransmit", "S12", 1, 2), ("ReverseReflect", "S22", 2, 2),
]


def four_parameter_held_frame(http, client, golden=None):
    query = "&".join("trace=" + quote(item[0], safe="") for item in TWO_PORT_MEASUREMENTS)
    frame = http.request("GET", "/api/v1/frame?" + query)
    assert frame["available"] and frame["current"]
    traces = {trace["name"]: trace for trace in frame["traces"]}
    assert list(traces) == [item[0] for item in TWO_PORT_MEASUREMENTS]
    for name, parameter, receiver, source in TWO_PORT_MEASUREMENTS:
        # HOLD 后的全部波量来自一个完整轮次；源 2 不能借用源 1 或本源 a1 的参考。
        reference = read_complex(client, f"Reference{source}")
        numerator = read_complex(client, f"Wave{receiver}{source}")
        expected = [b / a for a, b in zip(reference, numerator)]
        if golden is not None:
            point = golden[source - 1][0]
            component = (receiver - 1) * 4 + 2
            a_source = complex(*point[(source - 1) * 4:(source - 1) * 4 + 2])
            b_receiver = complex(*point[component:component + 2])
            expected = [b_receiver / a_source] * 2
            if source == 2:
                wrong_same_source = b_receiver / complex(*point[0:2])
                wrong_other_source = b_receiver / complex(*golden[0][0][4:6])
                assert abs(expected[0] - wrong_same_source) > 1e-3
                assert abs(expected[0] - wrong_other_source) > 1e-6
        trace = traces[name]
        assert (trace["parameter"], trace["receiverPort"], trace["sourcePort"]) == (parameter, receiver, source)
        assert trace["kind"] == "sParameter" and trace["unit"] == "1"
        assert trace["calibration"] == "uncalibrated"
        compare_complex(trace_complex(trace), expected, parameter + " HTTP independent ratio")
        compare_complex(read_complex(client, name), expected, parameter + " SCPI independent ratio")
    later = http.frame()
    assert all(frame[key] == later[key] for key in ("revision", "runId", "frameSeq"))
    assert client.query("SYST:ERR?").startswith("0,")


def selected_reverse_traces_preserve_identity(http, client):
    ordinary = http.frame()
    assert [trace["name"] for trace in ordinary["traces"]] == http.status()["display"]["displayedNames"]
    selected = http.request("GET", "/api/v1/frame?trace=ReverseReflect&trace=ReverseTransmit")
    assert selected["available"] and selected["current"]
    assert [trace["name"] for trace in selected["traces"]] == ["ReverseReflect", "ReverseTransmit"]
    for trace in selected["traces"]:
        compare_complex(trace_complex(trace), read_complex(client, trace["name"]), "Selected reverse trace")
    hidden = http.request("GET", "/api/v1/frame?display=none")
    assert hidden["traces"] == [] and hidden["frequencyHz"] == ordinary["frequencyHz"]
    for key in ("revision", "runId", "frameSeq"):
        assert ordinary[key] == selected[key] == hidden[key], "Display selection must preserve frame identity"


def four_parameter_frames(http, client, golden=None):
    for name in ("Reflect", "Transmit", "Reference", "Received", "Reflected"):
        assert int(client.query(f'CALC1:PAR:DEL "{name}";*OPC?')) == 1
    assert int(client.query('CALC1:PAR:DEF:EXT "DefaultA","a1,1";*OPC?')) == 1
    for name, parameter, _, _ in TWO_PORT_MEASUREMENTS:
        spelling = "S2_2" if parameter == "S22" else parameter
        assert int(client.query(f'CALC1:PAR:DEF:EXT "{name}","{spelling}";*OPC?')) == 1
    assert int(client.query("SENS1:SWE:MODE SING;*OPC?")) == 1
    selected_reverse_traces_preserve_identity(http, client)
    for source in (1, 2):
        definitions = [(f"Reference{source}", f"a{source},{source}")]
        definitions += [(f"Wave{receiver}{source}", f"b{receiver},{source}") for receiver in (1, 2)]
        for name, parameter in definitions:
            assert int(client.query(f'CALC1:PAR:DEF:EXT "{name}","{parameter}";*OPC?')) == 1
    assert int(client.query("SENS1:SWE:MODE SING;*OPC?")) == 1
    four_parameter_held_frame(http, client, golden)
    original_points = 2 if golden is not None else 17
    client.query(f"SENS1:SWE:POIN {original_points + 1};*OPC?")
    assert not http.frame()["current"], "Reverse data cannot reuse a previous configuration"
    client.socket.sendall(b'CALC1:PAR:SEL "ReverseTransmit";:CALC1:DATA? RDATA\n')
    assert client.query("SYST:ERR?").startswith("-230,"), "Stale S12 must be rejected"
    client.query(f"SENS1:SWE:POIN {original_points};MODE SING;*OPC?")
    four_parameter_held_frame(http, client, golden)
    if golden is not None:
        return
    client.query("SENS1:SWE:MODE CONT;*OPC?")
    deadline, sequences = time.monotonic() + 8, set()
    previous, changed = {}, set()
    while len(sequences) < 12:
        assert time.monotonic() < deadline, "Complete two-source frames stopped"
        query = "&".join("trace=" + quote(item[0], safe="") for item in TWO_PORT_MEASUREMENTS)
        frame = http.request("GET", "/api/v1/frame?" + query)
        if not frame["available"] or not frame["current"]:
            time.sleep(0.002)
            continue
        assert len(frame["traces"]) == 4
        for trace in frame["traces"]:
            values = trace_complex(trace)
            name = trace["name"]
            if name in previous and values != previous[name]:
                changed.add(name)
            previous[name] = values
        sequences.add(frame["frameSeq"])
    assert changed == {item[0] for item in TWO_PORT_MEASUREMENTS}, "Four noisy parameters must all update"
    assert int(client.query("SENS1:SWE:MODE HOLD;*OPC?")) == 1
    four_parameter_held_frame(http, client)


def exercise(server, driver, vectors, mode):
    environment = os.environ.copy()
    environment["VNAPRO_SIM_MODE"] = mode
    environment["VNAPRO_SIM_SEED"] = "42"
    with running_server(server, driver, environment, config=Path(__file__).with_name("golden_test.yml")) as ports:
        http = HttpClient(ports["http"])
        client = connect_scpi(ports["scpi"])
        try:
            prepare(client, mode == "noise")
            held_frame(http, client, vectors[0][0] if mode == "golden" else None)
            if mode == "noise":
                continuous_frames(http, client)
            else:
                unsupported_and_stale(http, client)
            # 单端旧验收保持完整，再切换四参数布局验证实际双源完整轮次。
            if mode == "golden":
                client.query("SENS1:SWE:POIN 2;*OPC?")
            four_parameter_frames(http, client, vectors if mode == "golden" else None)
        finally:
            client.close()


def main():
    assert len(sys.argv) == 4, "usage: SParameterIntegrationTest.py <server> <driver> <golden_iq_vectors.h>"
    vectors = golden_vectors(sys.argv[3])
    for mode in ("golden", "noise"):
        exercise(sys.argv[1], sys.argv[2], vectors, mode)
    print("Two-port four-parameter acceptance passed: independent same-source ratios, HTTP/SCPI, golden/noise")


if __name__ == "__main__":
    main()
