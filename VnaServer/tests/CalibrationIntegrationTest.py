"""验证实际 TCP/HTTP 校准合同及最大系数文件往返；外部测试集不代表硬件标准。"""

from http.client import HTTPConnection
import base64
import csv
import io
import json
import math
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from vnb_calibration_file import decode_calibration, download_bytes, file_fields, encode_calibration

from GoldenIqIntegrationTest import running_server
from WebHttpIntegrationTest import HttpClient, accepted, connect_scpi, wait_no_owner, wait_operation


def large_document(identity, points):
    # 每项都是高精度有限数，确保最大点数编码真实越过旧 4 MiB 上限。
    four = points == 10001
    row = [0.12345678901234567 + index * 0.00123456789012345 for index in range(96 if four else 24)]
    value = {
        "id": "0", "name": f"Transport vector {points}", "deviceIdentity": identity,
        "ifBandwidthHz": 1000, "includeIsolation": True,
        "method": "fourPort" if four else "twoPort", "port1": 1 if four else 2, "port2": 4,
        "frequencyHz": [10_000_000 + index * 1000 for index in range(points)],
        "powerDbm": [-12.375] * points,
        "onePortTerms": [], "twoPortTerms": [] if four else [row] * points,
    }
    if four:
        value["fourPortTerms"] = [row] * points
    return value


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
        return decode_calibration(download_bytes(json.loads(raw))), len(raw)
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
        "id": "0", "name": "Independent scaled reflection", "deviceIdentity": identity,
        "ifBandwidthHz": 1000, "includeIsolation": False, "method": "onePort", "port1": 1, "port2": 0,
        "frequencyHz": frame["frequencyHz"], "powerDbm": [0.0] * points,
        "onePortTerms": [[0, 0, 0, 0, 2, 0]] * points, "twoPortTerms": [],
    }


def import_and_enable_reflection(http, token, document):
    request = action(http, "importFile", "import-reflection", **file_fields(document))
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
    # 截掉一个系数的wire字节；不得靠测试侧encoder先拒绝畸形输入而漏过服务入口。
    bad = base64.b64encode(encode_calibration(document)[:-8]).decode("ascii")
    http.request("POST", "/api/v1/calibration", action(
        http, "importFile", "bad-row", format="binary", encoding="base64", content=bad
    ), token, expected=400)
    assert http.status()["calibration"] == unchanged


def large_roundtrips(http, token, identity):
    for count in (201, 10001):
        imported = large_document(identity, count)
        body = action(http, "importFile", f"large-{count}", **file_fields(imported))
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
                       action(http, "importFile", f"roundtrip-{count}", **file_fields(exported)), token), token)


def near_limit_csv(identity):
    """合法的近32 MiB CSV；长零指数不改变10001点单位校准的数值。"""
    points = 10001
    unit = [float(component == 4) for _ in range(16) for component in range(6)]
    expected = {
        "id": "1", "name": "CSV envelope boundary", "deviceIdentity": identity,
        "ifBandwidthHz": 1000, "includeIsolation": True, "method": "fourPort", "port1": 1, "port2": 4,
        "frequencyHz": [10_000_000 + index * 1000 for index in range(points)],
        "powerDbm": [0.0] * points, "onePortTerms": [], "twoPortTerms": [], "fourPortTerms": [unit] * points,
    }
    output = io.StringIO(newline="")
    output.write("\ufeff")
    writer = csv.writer(output, lineterminator="\r\n")
    for row in (
        ("vnapro_calibration_csv", "1"), ("source_id", "text:1"), ("name", "text:" + expected["name"]),
        ("device_identity", "text:" + identity), ("method", "fourPort"), ("port1", "1"), ("port2", "4"),
        ("include_isolation", "true"), ("if_bandwidth_hz", "1000"), ("points", str(points)),
    ):
        writer.writerow(row)
    columns = ["frequency_hz", "power_dbm"]
    for receiver in range(1, 5):
        for source in range(1, 5):
            terms = ("directivity", "source_match", "reflection_tracking") if receiver == source else (
                "isolation", "load_match", "transmission_tracking"
            )
            columns.extend(f"S{receiver}{source}_{term}_{part}" for term in terms for part in ("real", "imag"))
    writer.writerow(columns)
    short = [str(int(value)) + "e+" + "0" * 30 for value in unit]
    long = [value + "0" for value in short]
    target = 32 * 1024 * 1024 - 4096
    baseline = len(output.getvalue().encode("utf-8")) + sum(
        len(str(frequency)) + 3 + len(",".join(short)) + 2 for frequency in expected["frequencyHz"]
    )
    extra = target - baseline
    assert 0 <= extra <= points * len(unit)
    tails = {}
    for frequency in expected["frequencyHz"]:
        count = min(len(unit), extra)
        extra -= count
        if count not in tails:
            tails[count] = ",".join(long[:count] + short[count:])
        output.write(f"{frequency},0,{tails[count]}\r\n")
    content = output.getvalue()
    assert extra == 0 and len(content.encode("utf-8")) == target
    return content, expected


def csv_envelope_roundtrip(http, token, identity):
    content, expected = near_limit_csv(identity)
    before = http.status()["calibration"]
    body = action(http, "importFile", "csv-near-32mib", format="csv", encoding="utf8", content=content)
    encoded = json.dumps(body, separators=(",", ":"))
    assert 32 * 1024 * 1024 < len(encoded.encode("utf-8")) < 36 * 1024 * 1024, (
        "合法CSV的JSON封套必须越过旧32 MiB正文限制，且处于新的36 MiB封套预算内"
    )
    # 使用原HttpClient的5秒连接预算，禁止用更长超时或重试掩盖真实传输/解析问题。
    wait_operation(http, accepted(http, "calibration", encoded, token), token)
    after = http.status()["calibration"]
    assert len(after["sets"]) == len(before["sets"]) + 1
    assert all(after[field] == before[field] for field in ("selectedSetId", "enabled", "compatible"))
    set_id = after["sets"][-1]["id"]
    exported, _ = export_document(http, set_id)
    expected["id"] = set_id
    assert exported == expected, "近32 MiB CSV经真实HTTP导入后，默认binary导出的全部10001点及单位系数必须一致"


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
            csv_envelope_roundtrip(http, token, identity)
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
    print("Calibration TCP/HTTP passed: raw vs corrected, CAS, 201/10001 point and near-32MiB CSV roundtrip")


if __name__ == "__main__":
    main()
