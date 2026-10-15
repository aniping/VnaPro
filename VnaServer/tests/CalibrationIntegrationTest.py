"""验证实际 TCP/HTTP 校准合同及最大系数文件往返；外部测试集不代表硬件标准。"""

from copy import deepcopy
from http.client import HTTPConnection
import json
import math
import os
import sys

from GoldenIqIntegrationTest import running_server
from WebHttpIntegrationTest import HttpClient, accepted, connect_scpi, wait_no_owner, wait_operation


def large_document(identity, points):
    # 每项都是高精度有限数，确保最大点数编码真实越过旧 4 MiB 上限。
    row = [0.12345678901234567 + index * 0.00123456789012345 for index in range(24)]
    return {
        "schema": 2, "id": "0", "name": f"Transport vector {points}", "deviceIdentity": identity,
        "ifBandwidthHz": 1000, "includeIsolation": True, "method": "twoPort", "port1": 2, "port2": 4,
        "frequencyHz": [10_000_000 + index * 1000 for index in range(points)],
        "powerDbm": [-12.375] * points,
        "onePortTerms": [], "twoPortTerms": [row] * points,
    }


def action(http, kind, request_id, **fields):
    return {"requestId": request_id, "expectedRevision": http.status()["calibration"]["revision"],
            "action": kind, **fields}


def export_document(http, set_id):
    # 最大文档分批编码需要比普通交互更长的读取预算；没有取消服务器自身超时。
    connection = HTTPConnection("127.0.0.1", http.port, timeout=20)
    try:
        connection.request("GET", "/api/v1/calibration/sets/" + set_id)
        response = connection.getresponse()
        assert response.status == 200, response.status
        raw = response.read()
        return json.loads(raw), len(raw)
    finally:
        connection.close()


def raw_measurement(http, scpi_port):
    client = connect_scpi(scpi_port)
    try:
        client.query('SENS1:SWE:MODE HOLD;:CALC1:PAR:DEF:EXT "Reflect","S11";*OPC?')
        client.query('CALC1:PAR:SEL "Reflect";:SENS1:SWE:MODE SING;*OPC?')
        raw = [float(value) for value in client.query("CALC1:DATA? RDATA").split(",")]
        frame = http.frame(["Reflect"])
        identity = http.status()["identity"]
    finally:
        client.close()
    return raw, frame, identity


def reflection_document(frame, identity):
    points = len(frame["frequencyHz"])
    return {
        "schema": 2, "id": "0", "name": "Independent scaled reflection", "deviceIdentity": identity,
        "ifBandwidthHz": 1000, "includeIsolation": False, "method": "onePort", "port1": 1, "port2": 0,
        "frequencyHz": frame["frequencyHz"], "powerDbm": [0.0] * points,
        "onePortTerms": [[0, 0, 0, 0, 2, 0]] * points, "twoPortTerms": [],
    }


def import_and_enable_reflection(http, token, document):
    request = action(http, "import", "import-reflection", data=document)
    operation = accepted(http, "calibration", request, token)
    wait_operation(http, operation, token)
    assert accepted(http, "calibration", request, token) == operation
    calibration = http.status()["calibration"]
    assert not calibration["enabled"] and calibration["selectedSetId"] == "0"
    set_id = calibration["sets"][-1]["id"]
    for kind, fields in (("select", {"setId": set_id}), ("enable", {"enabled": True})):
        wait_operation(http, accepted(http, "calibration", action(http, kind, kind, **fields), token), token)


def assert_corrected_readback(http, observer, frame, raw):
    assert observer.query("SENSE1:CORRECTION:STATE?") == "1"
    corrected = http.frame(["Reflect"])
    trace = corrected["traces"][0]
    assert trace["calibration"] == "calibrated" and all(trace["valid"])
    assert len(trace["real"]) == len(trace["imag"])
    values = [value for pair in zip(trace["real"], trace["imag"]) for value in pair]
    scpi = [float(value) for value in observer.query("CALC1:DATA? SDATA").split(",")]
    still_raw = [float(value) for value in observer.query("CALC1:DATA? RDATA").split(",")]
    assert still_raw == raw and len(scpi) == len(values) == len(raw)
    assert all(math.isfinite(value) and abs(value - raw[index] / 2) < 1e-12
               and abs(scpi[index] - value) < 1e-12 for index, value in enumerate(values))
    assert corrected["frameSeq"] == frame["frameSeq"]
    assert corrected["calibrationRevision"] != frame["calibrationRevision"]


def reject_bad_coefficient_row(http, token, document):
    unchanged = http.status()["calibration"]
    bad = deepcopy(document)
    bad["onePortTerms"][0] = [0] * 5
    http.request("POST", "/api/v1/calibration", action(http, "import", "bad-row", data=bad), token, expected=400)
    assert http.status()["calibration"] == unchanged


def large_roundtrips(http, token, identity):
    for count in (201, 10001):
        imported = large_document(identity, count)
        body = action(http, "import", f"large-{count}", data=imported)
        encoded = json.dumps(body, separators=(",", ":"))
        assert len(encoded) > 16384
        if count == 10001:
            assert len(encoded) > 4 * 1024 * 1024
        wait_operation(http, accepted(http, "calibration", body, token), token)
        set_id = http.status()["calibration"]["sets"][-1]["id"]
        exported, size = export_document(http, set_id)
        imported["id"] = set_id
        assert exported == imported, (count, size)
        # 用服务器生成的文档重新导入，覆盖解析节点、规范指纹及增量编码三道预算。
        wait_operation(http, accepted(http, "calibration",
                       action(http, "import", f"roundtrip-{count}", data=exported), token), token)


def reject_control_body_budget(http):
    # 只发送超限长度头，避免立即拒绝后未读请求体导致 Windows TCP RST 淹没 413。
    connection = HTTPConnection("127.0.0.1", http.port, timeout=5)
    try:
        connection.putrequest("POST", "/api/v1/config")
        connection.putheader("Content-Length", "17000")
        connection.endheaders()
        response = connection.getresponse()
        assert response.status == 413
        response.read()
    finally:
        connection.close()


def exercise(http_port, scpi_port):
    http = HttpClient(http_port)
    raw, frame, identity = raw_measurement(http, scpi_port)
    wait_no_owner(http)
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    try:
        observer = connect_scpi(scpi_port)
        try:
            document = reflection_document(frame, identity)
            import_and_enable_reflection(http, token, document)
            assert_corrected_readback(http, observer, frame, raw)
            reject_bad_coefficient_row(http, token, document)
            large_roundtrips(http, token, identity)
            reject_control_body_budget(http)
            off = action(http, "enable", "off", enabled=False)
            wait_operation(http, accepted(http, "calibration", off, token), token)
            assert observer.query("SENS1:CORR?") == "0"
        finally:
            observer.close()
    finally:
        http.request("POST", "/api/v1/control/release", {}, token)
    wait_no_owner(http)


def main():
    assert len(sys.argv) == 3, "usage: CalibrationIntegrationTest.py <server> <driver>"
    environment = os.environ.copy()
    environment["VNAPRO_SIM_MODE"] = "golden"
    with running_server(sys.argv[1], sys.argv[2], environment, board="tdc1vn10") as ports:
        exercise(ports["http"], ports["scpi"])
    print("Calibration TCP/HTTP passed: raw vs corrected, CAS, 201/10001 point coefficient roundtrip")


if __name__ == "__main__":
    main()
