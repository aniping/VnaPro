"""使用固定且独立计算的 IQ 字面量验证 DLL → Core → HTTP/ASCII SCPI 数据链路。

预期值取自 golden_iq_vectors.h，其来源记录在对应常量的注释中。
只解析字面量，不导入或重新计算正式代码中的信号公式。
"""

from contextlib import contextmanager
import json
import math
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import threading
import time

from WebHttpIntegrationTest import HttpClient, connect_scpi, identity_number
from server_startup import isolated_server, listening_port


def golden_vectors(path):
    text = Path(path).read_text(encoding="utf-8")
    match = re.search(r"g_goldenIqBySource\[4\]\[2\]\[16\]\s*=\s*(\{.*?\});", text, re.S)
    assert match, "Missing fixed 4x2x16 golden literal array"
    literal = re.sub(r"//[^\n]*", "", match.group(1)).replace("{", "[").replace("}", "]")
    vectors = json.loads(literal)
    assert len(vectors) == 4 and all(len(source) == 2 for source in vectors)
    assert all(len(point) == 16 and all(math.isfinite(value) for value in point)
               for source in vectors for point in source)
    frequencies = re.search(r"g_goldenIqFrequencies\[2\]\s*=\s*\{([^}]+)\}", text)
    assert frequencies and re.findall(r"\b(\d+)ULL\b", frequencies.group(1)) == ["1000000000", "2000000000"]
    return vectors


def compare(actual, expected, context):
    assert len(actual) == len(expected), (context, len(actual), len(expected))
    for index, (value, golden) in enumerate(zip(actual, expected)):
        assert math.isfinite(value) and abs(value - golden) <= 1e-12, (context, index, value, golden)


def exercise(http_port, scpi_port, vectors):
    http = HttpClient(http_port)
    client = connect_scpi(scpi_port)
    try:
        assert int(client.query("*CLS;:SENS1:SWE:MODE HOLD;*OPC?")) == 1
        # 黄金向量使用独立测试能力配置；允许起止频率相同，正式默认能力另行验收。
        assert int(client.query("SENS1:FREQ:STOP 1GHz;STAR 1GHz;:SENS1:SWE:POIN 2;*OPC?")) == 1
        assert int(client.query('CALC1:PAR:DEL "Trc1";*OPC?')) == 1
        client.query("FORM:DATA ASC,0;:TRIG:SOUR IMM;*OPC?")
        measurements = [(f"G{source}{kind}{receiver}", source, kind, receiver)
                        for source in range(1, 5) for kind in ("a", "b") for receiver in range(1, 5)]
        for name, source, kind, receiver in measurements:
            assert int(client.query(f'CALC1:PAR:DEF:EXT "{name}","{kind}{receiver},{source}";*OPC?')) == 1
        assert int(client.query("SENS1:SWE:MODE SING;*OPC?")) == 1
        assert client.query("SENS1:SWE:MODE?") == "HOLD"
        compare([float(value) for value in client.query("SENS1:X?").split(",")], [1e9, 1e9], "SCPI axis")
        # 初始时全部 32 个测量项共享同一个完整的四激励源逻辑帧。
        initial = http.frame()
        for name, source, kind, receiver in measurements:
            component = (receiver - 1) * 4 + (2 if kind == "b" else 0)
            pair = vectors[source - 1][0][component:component + 2]
            actual = client.query(f'CALC1:PAR:SEL "{name}";:CALC1:DATA? RDATA')
            compare([float(value) for value in actual.split(",")], pair * 2, name + " SCPI RI")
        selected = http.frame()
        assert all(selected[key] == initial[key] for key in ("revision", "runId", "frameSeq")), \
            "Selection must retain the complete frame identity"

        # 诊断读取显式选择本批四条；默认 frame 已改为仪器保存的显示集合。
        for offset in range(0, len(measurements), 4):
            if offset:
                for name, _, _, _ in measurements[offset - 4:offset]:
                    assert int(client.query(f'CALC1:PAR:DEL "{name}";*OPC?')) == 1
                assert int(client.query("SENS1:SWE:MODE SING;*OPC?")) == 1
            batch = measurements[offset:offset + 4]
            status, frame = http.status(), http.frame([item[0] for item in batch])
            assert frame["available"] is True and frame["current"] is True, frame
            assert frame["revision"] == status["acquisitionRevision"] and frame["runId"] == status["runId"]
            assert frame["measurementRevision"] == status["revision"]
            assert all(identity_number(frame[key]) > 0 for key in ("revision", "measurementRevision", "runId", "frameSeq"))
            assert frame["originalPointCount"] == frame["displayPointCount"] == 2 and not frame["decimated"]
            assert frame["frequencyHz"] == [1e9, 1e9]
            assert [trace["name"] for trace in frame["traces"]] == [item[0] for item in batch]
            for trace, (name, source, kind, receiver) in zip(frame["traces"], batch):
                assert (trace["sourcePort"], trace["receiverKind"], trace["receiverPort"]) == (source, kind, receiver)
                assert trace["available"] is True and trace["valid"] == [True, True]
                assert all(type(value) is bool for value in trace["valid"])
                component = (receiver - 1) * 4 + (2 if kind == "b" else 0)
                pair = vectors[source - 1][0][component:component + 2]
                compare(trace["real"], [pair[0]] * 2, name + " HTTP real")
                compare(trace["imag"], [pair[1]] * 2, name + " HTTP imag")
        assert client.query("SYST:ERR?").startswith("0,"), "Golden acceptance left an instrument error"
    finally:
        client.close()


@contextmanager
def running_server(server, driver, environment, config=None, board="tdc1vn08"):
    """返回新启动服务的监听端口；优雅关闭由生命周期测试验证。"""
    with isolated_server(server, driver, environment, config=config, board=board) as bundle:
        with listening_server(bundle) as ports:
            yield ports


@contextmanager
def listening_server(bundle):
    """实际进程只接收可执行文件，端口及驱动来自同目录 YAML。"""
    process = subprocess.Popen(
        [bundle.executable], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", env=bundle.environment,
    )
    lines = queue.Queue()

    def collect():
        for line in process.stdout:
            lines.put(line.rstrip())
        lines.put(None)

    threading.Thread(target=collect, daemon=True).start()
    startup = []
    try:
        deadline, ports = time.monotonic() + 10, {}
        while len(ports) != 2:
            line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
            assert line is not None, "Server exited before both listeners became ready"
            startup.append(line)
            listener = listening_port(line)
            if listener is not None:
                ports[listener[0]] = listener[1]
            assert time.monotonic() < deadline, "Both listeners did not start"
        yield ports
        assert process.poll() is None, "Server exited during acceptance"
    except Exception:
        print("Server startup:", startup)
        raise
    finally:
        # 此处只清理传输测试进程；等待操作完成和关闭射频由生命周期测试验证。
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main():
    assert len(sys.argv) == 4, "usage: GoldenIqIntegrationTest.py <server> <driver> <golden_iq_vectors.h>"
    vectors = golden_vectors(sys.argv[3])
    environment = os.environ.copy()
    environment["VNAPRO_SIM_MODE"] = "golden"
    environment.pop("VNAPRO_SIM_SEED", None)
    with running_server(sys.argv[1], sys.argv[2], environment,
                        config=Path(__file__).with_name("golden_test.yml")) as ports:
        exercise(ports["http"], ports["scpi"], vectors)
    print("Golden IQ acceptance passed: 4 sources x 8 receivers x 2 points, HTTP + ASCII SCPI, 1e-12")


if __name__ == "__main__":
    main()
