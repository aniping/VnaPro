"""通过真实 TCP 验收 CableTest 所需的 VNB 四端口 SCPI 和二进制数据合同。"""

import math
import os
import socket
import struct
import sys
from pathlib import Path

from CalibrationIntegrationTest import action
from CalibrationScpiFixture import four_port_fixture
from vnb_calibration_file import decode_calibration, download_bytes, file_fields
from GoldenIqIntegrationTest import listening_server
from server_startup import isolated_server
from WebHttpIntegrationTest import HttpClient, accepted, wait_no_owner, wait_operation

PARAMETERS = [
    (f"1_S{receiver}{source}", f"S{receiver}{source}")
    for receiver in range(1, 5)
    for source in range(1, 5)
]
SELECTORS = ",".join(str(index) for index in range(1, 17))
REPEATED_S11 = ",".join(["1"] * 16)
CALSET_NAME = "CableTest opaque calibration"
BUDGET_CALSET_NAME = CALSET_NAME + " 10001"
FOUR_PORT_CALSET_NAME = "Explicit four-port non-unit software fixture"


class Client:
    """按声明长度读取二进制，避免 payload 中的换行被误认为消息结束。"""

    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.socket.settimeout(20)
        self.buffer = b""

    def close(self):
        self.socket.close()

    def write(self, command):
        self.socket.sendall((command + "\n").encode("ascii"))

    def exact(self, count):
        while len(self.buffer) < count:
            block = self.socket.recv(65536)
            assert block, "VNB 在响应完整前关闭了连接"
            self.buffer += block
        result, self.buffer = self.buffer[:count], self.buffer[count:]
        return result

    def line(self):
        while b"\n" not in self.buffer:
            block = self.socket.recv(65536)
            assert block, "VNB 未完成 ASCII 响应"
            self.buffer += block
        result, self.buffer = self.buffer.split(b"\n", 1)
        return result.rstrip(b"\r").decode("ascii")

    def query(self, command):
        self.write(command)
        return self.line()

    def block(self, selectors, points, endian):
        expected = len(selectors.split(",")) * points * 2 * 8
        return self.block_response(f":CALC:DATA:MSD? '{selectors}'", expected, endian)

    def block_response(self, command, expected, endian):
        self.write(command)
        prefix = self.exact(2)
        assert prefix[:1] == b"#" and b"1" <= prefix[1:] <= b"9", prefix
        length_digits = self.exact(int(prefix[1:]))
        assert length_digits.isdigit(), length_digits
        assert int(length_digits) == expected, (length_digits, expected)
        payload = self.exact(expected)
        assert self.exact(1) == b"\n", "二进制块必须完整结束于 LF"
        result = struct.unpack(endian + "d" * (expected // 8), payload)
        assert all(math.isfinite(value) for value in result), "二进制测量含非有限数"
        # 紧随块后的普通响应也必须保持边界，不能遗留数据或重复终止符。
        assert self.query("*IDN?").startswith("VnaPro,")
        return result


def no_error(client):
    response = client.query("SYST:ERR?")
    assert response.startswith("0,"), response


def command(client, text):
    assert int(client.query(text + ";*OPC?")) == 1, text
    no_error(client)


def failed_query(client, text, code=-230):
    # 失败查询不返回虚构数据；下一条独立消息读取错误 FIFO。
    client.write(text)
    response = client.query("SYST:ERR?")
    assert response.startswith(str(code) + ","), (text, response)
    no_error(client)


def select_ascii(client, name, kind="SDATA"):
    command(client, "FORM ASCII")
    command(client, f"CALC1:PAR:SEL '{name}'")
    values = tuple(
        float(value) for value in client.query(f"CALC1:DATA? {kind}").split(",")
    )
    assert all(math.isfinite(value) for value in values)
    no_error(client)
    return values


def frame_identity(http):
    frame = http.frame([name for name, _ in PARAMETERS])
    assert frame["available"] and frame["current"] and len(frame["traces"]) == 16, frame
    return frame["revision"], frame["runId"], frame["frameSeq"]


def define_matrix(client):
    command(client, "CALC1:PAR:DEL:ALL")
    assert client.query("CALC1:PAR:CAT:EXT?") in ('""', '"NO CATALOG"')
    for name, parameter in PARAMETERS:
        command(client, f"CALC1:PAR:DEF:EXT '{name}','{parameter}'")
    catalog = client.query("CALC1:PAR:CAT:EXT?")
    assert (
        catalog == '"' + ",".join(value for item in PARAMETERS for value in item) + '"'
    ), catalog


def prepare_sweep(client, points):
    command(client, "INIT1:CONT OFF")
    assert client.query("INIT1:CONT?") == "0"
    command(client, "SENS1:FREQ:STOP 100MHz;STAR 1MHz")
    command(client, f"SENS1:SWE:POIN {points}")
    assert float(client.query("SENS1:FREQ:STAR?")) == 1_000_000
    assert float(client.query("SENS1:FREQ:STOP?")) == 100_000_000
    assert int(float(client.query("SENS1:SWE:POIN?"))) == points
    assert float(client.query("SENS1:BWID?")) == 1000


def compare_blocks(client, http, points):
    before = frame_identity(http)
    expected = select_ascii(client, PARAMETERS[0][0])
    assert len(expected) == points * 2
    command(client, "FORM REAL,64")
    for order, endian in (("SWAPped", "<"), ("NORM", ">")):
        command(client, ":FORMat:BORDer " + order)
        assert client.block("1", points, endian) == expected, order
        assert client.block(REPEATED_S11, points, endian) == expected * 16, order
    failed_query(client, f"CALC:DATA:MSD? '{SELECTORS}'")
    failed_query(client, "CALC:DATA:MSD? '1,6'")
    assert frame_identity(http) == before, "读取多个迹线或切换格式不能重新采集"
    no_error(client)


def maximum_block(client, http):
    before = frame_identity(http)
    first = select_ascii(client, PARAMETERS[0][0])
    command(client, "FORM REAL,64;:FORM:BORD SWAP")
    # 重复同一条已校准 S11 只验证最大传输预算，不证明完整四端口校准。
    result = client.block(REPEATED_S11, 10001, "<")
    assert result == first * 16
    assert frame_identity(http) == before
    client.write("SENS1:SWE:POIN 10002")
    assert client.query("SYST:ERR?").startswith("-222,")
    assert int(float(client.query("SENS1:SWE:POIN?"))) == 10001
    no_error(client)


def matrix_roundtrip(client, http):
    command(client, "*CLS;:INIT1:CONT OFF")
    define_matrix(client)
    prepare_sweep(client, 17)
    command(client, "FORM REAL,64")
    failed_query(client, "CALC1:DATA? RDATA")
    failed_query(client, f"CALC:DATA:MSD? '{SELECTORS}'")
    command(client, "INIT1:IMM")
    before = frame_identity(http)
    for name, _ in PARAMETERS:
        raw = select_ascii(client, name, "RDATA")
        assert len(raw) == 34
        command(client, "FORM REAL,64;:FORM:BORD SWAP")
        assert client.block_response("CALC1:DATA? RDATA", 17 * 16, "<") == raw
    assert frame_identity(http) == before, "16 条原始迹线必须属于同一完整帧"
    command(client, "FORM REAL,64")
    failed_query(client, f"CALC:DATA:MSD? '{SELECTORS}'")
    failed_query(client, "CALC:DATA:MSD? '0'", -224)
    failed_query(client, "CALC:DATA:MSD? '17'")
    client.write("CALC1:PAR:SEL '1_s11'")
    assert not client.query("SYST:ERR?").startswith("0,"), "测量名必须区分大小写"
    no_error(client)
    command(client, "SENS1:SWE:POIN 18")
    failed_query(client, "CALC1:DATA? RDATA")
    failed_query(client, f"CALC:DATA:MSD? '{SELECTORS}'")
    command(client, "SENS1:SWE:POIN 17")
    command(client, "INIT1:IMM")
    return select_ascii(client, PARAMETERS[0][0], "RDATA")


def import_calibration(http, name):
    wait_no_owner(http)
    frame = http.frame([PARAMETERS[0][0]])
    points = len(frame["frequencyHz"])
    document = {
        "id": "0",
        "name": name,
        "deviceIdentity": http.status()["identity"],
        "ifBandwidthHz": 1000,
        "includeIsolation": False,
        "method": "onePort",
        "port1": 1,
        "port2": 0,
        "frequencyHz": frame["frequencyHz"],
        "powerDbm": [0.0] * points,
        "onePortTerms": [[0, 0, 0, 0, 2, 0]] * points,
        "twoPortTerms": [],
    }
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    try:
        body = action(http, "importFile", "vnb-import-calset-" + str(points), **file_fields(document))
        wait_operation(http, accepted(http, "calibration", body, token), token)
        return http.status()["calibration"]["sets"][-1]["id"]
    finally:
        http.request("POST", "/api/v1/control/release", {}, token)


def calibration_roundtrip(client, http, raw, set_id):
    catalog = client.query("SENS1:CORR:CSET:CAT?")
    assert CALSET_NAME in catalog and set_id in catalog, catalog
    command(client, f"SENS1:CORR:CSET:ACT '{CALSET_NAME}',0")
    assert client.query("SENS1:CORR:STAT?") == "1"
    corrected = select_ascii(client, PARAMETERS[0][0])
    assert corrected == tuple(value / 2 for value in raw), "ACT 必须实际校正 SDATA"
    assert select_ascii(client, PARAMETERS[0][0], "RDATA") == raw
    compare_blocks(client, http, 17)
    selected = client.query("SENS1:CORR:CSET:ACT?")
    client.write("SENS1:CORR:CSET:ACT 'missing calset',0")
    assert not client.query("SYST:ERR?").startswith("0,"), "未知校准集不能伪装激活成功"
    assert client.query("SENS1:CORR:CSET:ACT?") == selected
    no_error(client)
    command(client, "SENS1:SWE:POIN 18")
    client.write(f"SENS1:CORR:CSET:ACT '{CALSET_NAME}',0")
    assert not client.query("SYST:ERR?").startswith("0,"), (
        "不匹配刺激的校准集必须拒绝激活"
    )
    assert client.query("SENS1:CORR:STAT?") == "0"
    no_error(client)


def reset_preserves_calsets(client):
    catalog = client.query("SENS1:CORR:CSET:CAT?")
    assert CALSET_NAME in catalog and BUDGET_CALSET_NAME in catalog
    command(client, "*RST")
    assert client.query("SENS1:CORR:CSET:CAT?") == catalog, "RST 不得删除已有校准集"
    assert PARAMETERS[0][0] not in client.query("CALC1:PAR:CAT:EXT?"), (
        "RST 必须清理旧测量定义"
    )
    prepare_sweep(client, 17)
    command(client, f"SENS1:CORR:CSET:ACT '{CALSET_NAME}',0")
    assert client.query("SENS1:CORR:STAT?") == "1", "RST 后保存的校准集必须仍可真实激活"
    command(client, "CALC1:PAR:DEL:ALL")
    assert client.query("CALC1:PAR:CAT:EXT?") in ('""', '"NO CATALOG"')
    assert client.query("SENS1:CORR:CSET:CAT?") == catalog


def four_port_roundtrip(ports, http):
    client = Client(ports["scpi"])
    try:
        prepare_sweep(client, 17)
        define_matrix(client)
        command(client, "INIT1:IMM")
        raw = [select_ascii(client, name, "RDATA") for name, _ in PARAMETERS]
        before = frame_identity(http)
    finally:
        client.close()
    wait_no_owner(http)
    frame = http.frame([PARAMETERS[0][0]])
    document, expected = four_port_fixture(http, frame, raw, FOUR_PORT_CALSET_NAME)
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    try:
        body = action(http, "importFile", "four-port-explicit-fixture", **file_fields(document))
        wait_operation(http, accepted(http, "calibration", body, token), token)
        set_id = http.status()["calibration"]["sets"][-1]["id"]
        exported = decode_calibration(download_bytes(http.request("GET", "/api/v1/calibration/sets/" + set_id)))
        assert (
            exported["method"] == "fourPort"
            and exported["fourPortTerms"] == document["fourPortTerms"]
        )
    finally:
        http.request("POST", "/api/v1/control/release", {}, token)
    wait_no_owner(http)
    client = Client(ports["scpi"])
    try:
        command(client, f"SENS1:CORR:CSET:ACT '{FOUR_PORT_CALSET_NAME}',0")
        command(client, "FORM REAL,64;:FORM:BORD SWAP")
        result = client.block(SELECTORS, 17, "<")
        assert len(result) == 16 * 17 * 2
        assert all(
            abs(actual - wanted) < 1e-12 for actual, wanted in zip(result, expected)
        )
        assert frame_identity(http) == before, "四口校正应重用同一不可变完整帧"
        command(client, "FORM ASCII")
        for index, (name, _) in enumerate(PARAMETERS):
            corrected = select_ascii(client, name)
            assert corrected == tuple(result[index * 34 : (index + 1) * 34])
            assert select_ascii(client, name, "RDATA") == raw[index]
        no_error(client)
        command(client, "*RST")
        assert FOUR_PORT_CALSET_NAME in client.query("SENS1:CORR:CSET:CAT?")
    finally:
        client.close()


def exercise(ports):
    http = HttpClient(ports["http"])
    client = Client(ports["scpi"])
    try:
        assert client.query("*IDN?").startswith("VnaPro,")
        raw = matrix_roundtrip(client, http)
    finally:
        client.close()
    set_id = import_calibration(http, CALSET_NAME)
    wait_no_owner(http)
    client = Client(ports["scpi"])
    try:
        calibration_roundtrip(client, http, raw, set_id)
        command(client, "SENS1:SWE:POIN 10001")
        command(client, "FORM REAL,64")
        failed_query(client, "CALC:DATA:MSD? '1'")
        command(client, "INIT1:IMM")
        failed_query(client, "CALC:DATA:MSD? '1'")
    finally:
        client.close()
    import_calibration(http, BUDGET_CALSET_NAME)
    wait_no_owner(http)
    client = Client(ports["scpi"])
    try:
        command(client, f"SENS1:CORR:CSET:ACT '{BUDGET_CALSET_NAME}',0")
        maximum_block(client, http)
        reset_preserves_calsets(client)
    finally:
        client.close()
    wait_no_owner(http)
    four_port_roundtrip(ports, http)


def main():
    assert len(sys.argv) == 3, (
        "usage: VnbScpiIntegrationTest.py <server> <VN10 simulator>"
    )
    environment = dict(os.environ, VNAPRO_SIM_MODE="noise", VNAPRO_SIM_SEED="42")
    config = Path(__file__).with_name("golden_test.yml")
    with (
        isolated_server(
            sys.argv[1], sys.argv[2], environment, config=config, board="tdc1vn10"
        ) as bundle,
        listening_server(bundle) as ports,
    ):
        exercise(ports)
    print(
        "VNB SCPI TCP passed: 16 raw and non-unit four-port calibrated traces, REAL64 layout, strict coverage and reset"
    )


if __name__ == "__main__":
    main()
