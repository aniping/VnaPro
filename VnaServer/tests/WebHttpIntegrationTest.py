"""联合验证真实类型化 HTTP API 和仪器 TCP SCPI 服务。"""

import errno
from http.client import HTTPConnection
from html.parser import HTMLParser
import json
import math
import os
import queue
import socket
import subprocess
from pathlib import Path
import sys
import threading
import time
from urllib.parse import quote

from server_startup import listening_port


def reject_constant(value):
    raise AssertionError("Non-finite JSON number: " + value)


class HttpClient:
    def __init__(self, port):
        self.port = port

    def request(self, method, path, body=None, token=None, expected=200, raw=False):
        headers = {}
        if token is not None:
            headers["X-Vna-Session"] = token
        if body is not None:
            headers["Content-Type"] = "application/json"
            body = body if isinstance(body, str) else json.dumps(body, separators=(",", ":"))
        connection = HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            connection.request(method, path, body=body, headers=headers)
            response = connection.getresponse()
            data = response.read()
            allowed = (expected,) if isinstance(expected, int) else expected
            assert response.status in allowed, (method, path, response.status, data[:500])
            media = response.getheader("Content-Type", "").split(";", 1)[0].strip().lower()
            if raw:
                return data, media
            assert media == "application/json", (path, media)
            result = json.loads(data.decode("utf-8"), parse_constant=reject_constant)
            if response.status >= 400:
                assert isinstance(result.get("error"), dict), result
                assert "code" in result["error"] and "message" in result["error"], result
            return result
        finally:
            connection.close()

    def status(self, token=None):
        return self.request("GET", "/api/v1/status", token=token)

    def frame(self):
        return self.request("GET", "/api/v1/frame")


class AssetLinks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = set()

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if tag == "link" and attributes.get("rel") == "stylesheet":
            self.links.add(attributes.get("href"))
        if tag == "script" and "src" in attributes:
            self.links.add(attributes["src"])


class ScpiClient:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=2)
        self.socket.settimeout(2)
        self.buffer = b""

    def close(self):
        self.socket.close()

    def query(self, command):
        self.socket.sendall((command + "\n").encode("ascii"))
        while b"\n" not in self.buffer:
            block = self.socket.recv(8192)
            assert block, "SCPI controller was rejected"
            self.buffer += block
        result, self.buffer = self.buffer.split(b"\n", 1)
        return result.rstrip(b"\r").decode("ascii")


def connect_scpi(port):
    deadline = time.monotonic() + 8
    while True:
        client = None
        try:
            client = ScpiClient(port)
            assert client.query("*IDN?").startswith("VnaPro,")
            return client
        except (OSError, AssertionError):
            if client is not None:
                client.close()
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.01)


def assert_scpi_rejected(port):
    client = None
    try:
        client = ScpiClient(port)
        client.socket.sendall(b"*IDN?\n")
        assert client.socket.recv(32) == b"", "Web control must exclude a SCPI controller"
    except OSError as error:
        assert error.errno in (errno.ECONNRESET, errno.ECONNABORTED, errno.EPIPE) or getattr(
            error, "winerror", None
        ) in (10053, 10054, 10058), error
    finally:
        if client is not None:
            client.close()


def identity_number(value):
    assert isinstance(value, str) and value.isdecimal(), value
    return int(value)


def config_tuple(status):
    config = status["config"]
    return tuple(config[key] for key in ("startHz", "stopHz", "points", "ifBandwidthHz"))


def wait_operation(http, operation_id, token, check=None):
    deadline = time.monotonic() + 10
    while True:
        if check is not None:
            check()
        operation = http.request("GET", "/api/v1/operations/" + quote(operation_id, safe=""), token=token)
        assert operation["operationId"] == operation_id, operation
        assert operation["state"] in ("pending", "completed", "failed"), operation
        if operation["state"] != "pending":
            assert operation["state"] == "completed" and operation["result"]["ok"] is True, operation
            return operation
        assert time.monotonic() < deadline, operation
        # 轮询协议状态；此间隔不用于推断 I/O 是否完成。
        time.sleep(0.01)


def accepted(http, path, body, token):
    response = http.request("POST", "/api/v1/" + path, body, token, expected=202)
    assert isinstance(response["operationId"], str) and response["operationId"], response
    return response["operationId"]


def wait_no_owner(http):
    deadline = time.monotonic() + 8
    while True:
        status = http.status()
        if status["control"]["owner"] == "none" and not status["control"]["releasing"]:
            return status
        assert time.monotonic() < deadline, status
        time.sleep(0.01)


def check_frame(frame, status, points):
    assert frame["available"] is True and frame["current"] is True, frame
    assert frame["revision"] == status["acquisitionRevision"] and frame["runId"] == status["runId"], frame
    assert frame["measurementRevision"] == status["revision"], frame
    for key in ("revision", "measurementRevision", "runId", "frameSeq"):
        assert identity_number(frame[key]) > 0, (key, frame[key])
    assert frame["originalPointCount"] == points, frame
    assert frame["displayPointCount"] == points and frame["decimated"] is False, frame
    axis = frame["frequencyHz"]
    config = status["config"]
    # 已验证的模拟设备配置会将提交的激励频率量化到整数 Hz。
    expected_axis = [
        math.floor(config["startHz"] + index * (config["stopHz"] - config["startHz"]) / (points - 1) + 0.5)
        for index in range(points)
    ]
    assert axis == expected_axis, axis
    traces = {trace["name"]: trace for trace in frame["traces"]}
    assert "Web A" in traces and "Web B" in traces, traces.keys()
    for trace in traces.values():
        assert trace["available"] is True, trace
        assert len(trace["real"]) == len(trace["imag"]) == len(trace["valid"]) == points, trace
        assert all(type(value) is bool and value for value in trace["valid"]), trace
        assert all(math.isfinite(value) for value in trace["real"] + trace["imag"]), trace
    assert traces["Web A"]["real"] != traces["Web B"]["real"], "Do not fabricate identical receiver curves"
    return traces


def define_receiver(http, token, name, wave, receiver, source):
    body = {
        "name": name, "kind": "receiver", "receiverKind": wave,
        "receiverPort": receiver, "sourcePort": source,
        "expectedRevision": http.status()["revision"], "requestId": "live-define-" + name,
    }
    wait_operation(http, accepted(http, "measurements", body, token), token)


def wait_current_frame(http, revision):
    deadline = time.monotonic() + 8
    while True:
        frame = http.frame()
        if frame["available"] and frame["current"] and frame["measurementRevision"] == revision:
            return frame
        assert time.monotonic() < deadline, frame
        time.sleep(0.002)


def switch_live_parameter(http, token, before, receiver, source, request_id):
    body = {
        "kind": "sParameter", "receiverKind": "b", "receiverPort": receiver, "sourcePort": source,
        "expectedRevision": before["revision"], "requestId": request_id,
    }
    path = "/api/v1/measurements/Trc1"
    response = http.request("PUT", path, body, token, expected=202)

    def check_continuous():
        status = http.status()
        assert status["config"]["sweepMode"] == "CONT" and status["runId"] == before["runId"], status

    wait_operation(http, response["operationId"], token, check_continuous)
    after = http.status()
    check_continuous()
    assert identity_number(after["revision"]) == identity_number(before["revision"]) + 1, after
    assert [item["name"] for item in after["measurements"]] == [item["name"] for item in before["measurements"]]
    assert after["activeConfigValid"], after
    frame = wait_current_frame(http, after["revision"])
    assert frame["revision"] == after["acquisitionRevision"], frame
    assert frame["runId"] == before["runId"], frame
    stale = dict(body, requestId=request_id + "-stale")
    http.request("PUT", path, stale, token, expected=409)
    assert http.status()["revision"] == after["revision"], "Stale CAS must preserve the live measurement"
    return after, frame


def assert_trace_ratio(frame, reference_name, response_name, parameter):
    traces = {trace["name"]: trace for trace in frame["traces"]}
    ratio = traces["Trc1"]
    assert ratio["parameter"] == parameter and ratio["available"] and all(ratio["valid"]), ratio
    reference = traces[reference_name]
    response = traces[response_name]
    count = frame["displayPointCount"]
    assert count > 0, frame
    for trace in (ratio, reference, response):
        assert trace["available"] and all(trace["valid"]), trace
        assert all(len(trace[key]) == count for key in ("real", "imag", "valid")), trace
    for values in zip(ratio["real"], ratio["imag"], reference["real"], reference["imag"],
                      response["real"], response["imag"]):
        real, imaginary, a_real, a_imaginary, b_real, b_imaginary = values
        expected = complex(b_real, b_imaginary) / complex(a_real, a_imaginary)
        assert abs(complex(real, imaginary) - expected) < 1e-12, (parameter, values, expected)


def exercise_live_measurements(http, token):
    """连续采集时原位修改测量，分别验证复算与新增源端口的计划切换。"""
    receivers = (("Live a1", "a", 1, 1), ("Live b1", "b", 1, 1), ("Live b2", "b", 2, 1))
    for receiver in receivers:
        define_receiver(http, token, *receiver)
    wait_operation(http, accepted(http, "operations", {
        "action": "continuous", "requestId": "live-start-continuous",
    }, token), token)
    before = http.status()
    original = wait_current_frame(http, before["revision"])
    assert_trace_ratio(original, "Live a1", "Live b2", "S21")
    same_source, switched = switch_live_parameter(http, token, before, 1, 1, "live-S11")
    assert same_source["acquisitionRevision"] == before["acquisitionRevision"], "Covered inputs keep the hardware plan"
    assert_trace_ratio(switched, "Live a1", "Live b1", "S11")
    reverse, first_reverse = switch_live_parameter(http, token, same_source, 1, 2, "live-S12")
    assert identity_number(reverse["acquisitionRevision"]) > identity_number(same_source["acquisitionRevision"])
    deadline = time.monotonic() + 8
    while True:
        later = wait_current_frame(http, reverse["revision"])
        assert http.status()["config"]["sweepMode"] == "CONT", "Plan changes must preserve continuous mode"
        if identity_number(later["frameSeq"]) > identity_number(first_reverse["frameSeq"]):
            break
        assert time.monotonic() < deadline, "Reverse sweeps stopped after the update"
    reverse_receivers = (("Live a2", "a", 2, 2), ("Live b1rev", "b", 1, 2))
    for receiver in reverse_receivers:
        define_receiver(http, token, *receiver)
        status = http.status()
        assert status["config"]["sweepMode"] == "CONT" and status["runId"] == reverse["runId"], status
        assert status["acquisitionRevision"] == reverse["acquisitionRevision"], "Covered RAW additions do not reconfigure"
    wait_operation(http, accepted(http, "operations", {"action": "hold", "requestId": "live-freeze"}, token), token)
    frozen = http.frame()
    query = "&".join("trace=" + quote(name, safe="") for name in ("Trc1", "Live a2", "Live b1rev"))
    reverse_frame = http.request("GET", "/api/v1/frame?" + query)
    assert reverse_frame["current"] and reverse_frame["revision"] == frozen["revision"]
    assert reverse_frame["frameSeq"] == frozen["frameSeq"], "Trace filtering must reuse the frozen physical frame"
    assert_trace_ratio(reverse_frame, "Live a2", "Live b1rev", "S12")
    for receiver in receivers + reverse_receivers:
        response = http.request("DELETE", "/api/v1/measurements/" + quote(receiver[0], safe=""), {
            "expectedRevision": http.status()["revision"], "requestId": "live-delete-" + receiver[0],
        }, token, expected=202)
        wait_operation(http, response["operationId"], token)
    response = http.request("PUT", "/api/v1/measurements/Trc1", {
        "kind": "sParameter", "receiverKind": "b", "receiverPort": 2, "sourcePort": 1,
        "expectedRevision": http.status()["revision"], "requestId": "live-restore-S21",
    }, token, expected=202)
    wait_operation(http, response["operationId"], token)


def exercise_colon_measurement(http, token):
    """通过真实端点验证测量名称中的冒号，同时保持静态文件路径防护。"""
    initial_names = [item["name"] for item in http.status()["measurements"]]
    name = "Port:1"
    path = "/api/v1/measurements/" + quote(name, safe="")
    definition = {
        "name": name, "kind": "receiver", "receiverKind": "a", "receiverPort": 1, "sourcePort": 1,
        "expectedRevision": http.status()["revision"], "requestId": "define-colon-name",
    }
    wait_operation(http, accepted(http, "measurements", definition, token), token)
    before = http.status()
    names = [item["name"] for item in before["measurements"]]
    update = {
        "kind": "sParameter", "receiverKind": "b", "receiverPort": 2, "sourcePort": 2,
        "expectedRevision": before["revision"], "requestId": "update-colon-name",
    }
    response = http.request("PUT", path, update, token, expected=202)
    wait_operation(http, response["operationId"], token)
    updated = http.status()
    assert [item["name"] for item in updated["measurements"]] == names, updated
    assert identity_number(updated["revision"]) == identity_number(before["revision"]) + 1, updated
    measurement = next(item for item in updated["measurements"] if item["name"] == name)
    assert measurement["parameter"] == "S22" and measurement["kind"] == "sParameter", measurement
    response = http.request("DELETE", path, {
        "expectedRevision": updated["revision"], "requestId": "delete-colon-name",
    }, token, expected=202)
    wait_operation(http, response["operationId"], token)
    assert [item["name"] for item in http.status()["measurements"]] == initial_names
    for path in ("/assets/Port%3A1.js", "/assets/Port:1.js", "/api/v1/unknown%3Apath",
                 "/api/v1/measurements/Port%3A1/file"):
        http.request("GET", path, expected=400)


def exercise(http_port, scpi_port):
    http = HttpClient(http_port)
    page, media = http.request("GET", "/", raw=True)
    assert media == "text/html" and page, media
    links = AssetLinks()
    links.feed(page.decode("utf-8"))
    assert any(path.endswith(".css") for path in links.links), links.links
    assert any(path.endswith(".js") for path in links.links), links.links
    for path in links.links:
        assert path.startswith("/assets/") and ".." not in path, path
        expected_media = "text/css" if path.endswith(".css") else "text/javascript"
        data, media = http.request("GET", path, raw=True)
        assert data and media == expected_media, (path, media)
    http.request("GET", "/missing-file.js", expected=404, raw=True)

    initial = http.status()
    assert "VnaPro" in json.dumps(initial["identity"]), initial
    assert initial["control"]["owner"] == "none" and initial["control"]["hasControl"] is False, initial
    assert identity_number(initial["revision"]) > 0, initial
    assert identity_number(initial["acquisitionRevision"]) > 0, initial
    assert http.frame()["available"] is False, "An observer must not manufacture a measurement"
    assert http.status()["revision"] == initial["revision"], "Observer GET must not mutate configuration"

    scpi = connect_scpi(scpi_port)
    try:
        assert scpi.query("*CLS;THISCOMMANDDOESNOTEXIST;SYST:ERR:COUN?") == "1"
        for _ in range(2):
            assert http.status()["control"]["owner"] == "scpi"
            assert http.frame()["available"] is False
        http.request("POST", "/api/v1/control/claim", {}, expected=409)
        assert scpi.query("SYST:ERR:COUN?") == "1", "HTTP reads and conflicts must not consume SCPI errors"
    finally:
        scpi.close()
    wait_no_owner(http)
    claim = http.request("POST", "/api/v1/control/claim", {})
    token = claim["sessionId"]
    assert isinstance(token, str) and token and claim["hasControl"] is True and claim["leaseSeconds"] == 30, claim
    assert http.status()["control"]["hasControl"] is False, "Observers do not inherit the writer token"
    control = http.status(token)["control"]
    assert control["owner"] == "web" and control["hasControl"] is True and control["leaseRemainingMs"] > 0, control
    renewed = http.request("POST", "/api/v1/control/claim", {"sessionId": token}, token)
    assert renewed["sessionId"] == token and renewed["hasControl"] is True, renewed
    assert_scpi_rejected(scpi_port)

    before = http.status(token)
    patch = {
        "expectedRevision": before["revision"], "requestId": "atomic-config",
        "startHz": 2000000, "stopHz": 18000000, "points": 17, "ifBandwidthHz": 10000,
    }
    target = (2000000, 18000000, 17, 10000)
    operation_id = accepted(http, "config", patch, token)
    wait_operation(http, operation_id, token, lambda: assert_atomic(http, config_tuple(before), target))
    configured = http.status(token)
    assert config_tuple(configured) == target, configured
    assert identity_number(configured["revision"]) == identity_number(before["revision"]) + 1, configured
    assert accepted(http, "config", patch, token) == operation_id, "Retry must return the original operation"
    assert http.status()["revision"] == configured["revision"], "Retry must not reconfigure"
    http.request("POST", "/api/v1/config", dict(patch, points=19), token, expected=409)
    http.request("POST", "/api/v1/config", dict(patch, requestId="stale-revision"), token, expected=409)
    http.request("POST", "/api/v1/config", dict(patch, expectedRevision="0", requestId="zero-revision"),
                 token, expected=409)
    http.request("POST", "/api/v1/config", dict(patch, expectedRevision=configured["revision"],
                 requestId="observer-write"), expected=403)
    http.request("POST", "/api/v1/operations", "{broken-json", token, expected=400)
    http.request("DELETE", "/api/v1/config", dict(patch, expectedRevision=configured["revision"],
                 requestId="wrong-method-config"), token, expected=405)
    http.request("DELETE", "/api/v1/operations", {"action": "single", "requestId": "wrong-method-single"},
                 token, expected=405)
    assert http.status()["revision"] == configured["revision"] and http.frame()["available"] is False
    http.request("GET", "/api/v1/operations/" + quote(operation_id, safe=""), expected=403)
    http.request("GET", "/api/v1/operations/" + quote(operation_id, safe=""), token=token + "-other", expected=403)

    exercise_live_measurements(http, token)
    for name, kind, port in (("Web A", "a", 1), ("Web B", "b", 2)):
        measurement = {
            "name": name, "receiverKind": kind, "receiverPort": port, "sourcePort": port,
            "expectedRevision": http.status()["revision"], "requestId": "define-" + kind,
        }
        wait_operation(http, accepted(http, "measurements", measurement, token), token)
    exercise_colon_measurement(http, token)
    temporary = {
        "name": "Temporary curve", "receiverKind": "a", "receiverPort": 1, "sourcePort": 1,
        "expectedRevision": http.status()["revision"], "requestId": "temporary-measurement",
    }
    wait_operation(http, accepted(http, "measurements", temporary, token), token)
    deleted = http.request("DELETE", "/api/v1/measurements/" + quote(temporary["name"], safe=""),
                           {"expectedRevision": http.status()["revision"], "requestId": "delete-temporary"},
                           token, expected=202)
    wait_operation(http, deleted["operationId"], token)
    assert temporary["name"] not in {measurement["name"] for measurement in http.status()["measurements"]}
    single = accepted(http, "operations", {"action": "single", "requestId": "first-single"}, token)
    wait_operation(http, single, token)
    status = http.status()
    assert status["config"]["sweepMode"] == "HOLD", status
    frame = http.frame()
    check_frame(frame, status, 17)
    assert http.frame()["frameSeq"] == frame["frameSeq"], "HOLD observers must retain one complete logical frame"

    newer = {"expectedRevision": status["revision"], "requestId": "new-points", "points": 19}
    wait_operation(http, accepted(http, "config", newer, token), token)
    stale = http.frame()
    assert stale["current"] is False, "The old revision must not become the current valid curve"
    if stale["available"]:
        assert stale["revision"] == frame["revision"] and stale["frequencyHz"] == frame["frequencyHz"], stale
    second = accepted(http, "operations", {"action": "single", "requestId": "release-single"}, token)
    released = http.request("POST", "/api/v1/control/release", {}, token)
    assert released["releasing"] is True, released
    wait_operation(http, second, token)
    http.request("POST", "/api/v1/operations", {"action": "single", "requestId": "released-write"},
                 token, expected=(403, 409))
    scpi = connect_scpi(scpi_port)
    try:
        status = http.status()
        traces = check_frame(http.frame(), status, 19)
        assert float(scpi.query("SENS1:SWE:POIN?")) == 19, "The next owner inherits committed settings"
        assert scpi.query('CALC1:PAR:SEL "Web B";:SYST:ERR:COUN?') == "1"
        actual = [float(value) for value in scpi.query("CALC1:DATA? RDATA").split(",")]
        expected = [value for pair in zip(traces["Web B"]["real"], traces["Web B"]["imag"]) for value in pair]
        assert actual == expected, "HTTP and SCPI must read the same frozen measurement"
        assert scpi.query("SYST:ERR?").startswith("-113,"), "Web requests must preserve the instrument error FIFO"
        assert scpi.query("SYST:ERR:COUN?") == "0"
        assert int(scpi.query("SENS1:SWE:POIN 2501;*OPC?")) == 1
        assert int(scpi.query("SENS1:SWE:MODE SING;*OPC?")) == 1
        reduced = http.frame()
        status = http.status()
        assert reduced["current"] is True and reduced["decimated"] is True, reduced
        assert reduced["originalPointCount"] == 2501 and reduced["displayPointCount"] == 2001, reduced
        assert reduced["revision"] == status["acquisitionRevision"] and len(reduced["traces"]) <= 4, reduced
        assert reduced["measurementRevision"] == status["revision"], reduced
        full_axis = [float(value) for value in scpi.query("SENS1:X?").split(",")]
        full_iq = [float(value) for value in scpi.query("CALC1:DATA? RDATA").split(",")]
        samples = {hz: (full_iq[2 * index], full_iq[2 * index + 1]) for index, hz in enumerate(full_axis)}
        receiver = next(trace for trace in reduced["traces"] if trace["name"] == "Web B")
        assert len(reduced["frequencyHz"]) == len(receiver["real"]) == len(receiver["imag"]) == 2001
        for hz, real, imaginary in zip(reduced["frequencyHz"], receiver["real"], receiver["imag"]):
            assert samples[hz] == (real, imaginary), "Display decimation must preserve the physical receiver samples"
        assert all(receiver["valid"]), receiver
    finally:
        scpi.close()

    for path in ("/../CMakeLists.txt", "/%2e%2e/CMakeLists.txt", "/..%5cCMakeLists.txt"):
        http.request("GET", path, expected=(400, 403, 404), raw=True)
    http.request("PUT", "/api/v1/status", {}, expected=(400, 404, 405))
    http.request("GET", "/api/v1/does-not-exist", expected=404)
    connection = HTTPConnection("127.0.0.1", http_port, timeout=3)
    try:
        connection.putrequest("POST", "/api/v1/config")
        connection.putheader("Content-Type", "application/json")
        connection.putheader("Content-Length", "16385")
        connection.endheaders()
        response = connection.getresponse()
        assert response.status == 413, "Oversized declared body must be rejected before waiting for bytes"
        response.read()
    finally:
        connection.close()
    assert http.status()["revision"] == status["revision"], "Rejected HTTP input must not mutate the instrument"


def assert_atomic(http, previous, target):
    assert config_tuple(http.status()) in (previous, target), "An atomic patch exposed partially committed settings"


def main():
    environment = dict(os.environ, VNAPRO_SIM_MODE="golden")
    environment.pop("VNAPRO_SIM_SEED", None)
    process = subprocess.Popen(
        [sys.argv[1], "--port", "0", "--web-port", "0", "--driver", os.path.abspath(sys.argv[2]),
         "--web-root", os.path.abspath(sys.argv[3]),
         "--config", os.fspath(Path(__file__).with_name("golden_test.yml"))],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", env=environment,
    )
    lines = queue.Queue()

    def collect():
        for line in process.stdout:
            lines.put(line.rstrip())
        lines.put(None)

    threading.Thread(target=collect, daemon=True).start()
    startup = []
    try:
        deadline = time.monotonic() + 10
        ports = {}
        while len(ports) != 2:
            line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
            assert line is not None, "Server exited before both listeners became ready"
            startup.append(line)
            listener = listening_port(line)
            if listener is not None:
                ports[listener[0]] = listener[1]
            assert time.monotonic() < deadline, "Both listeners did not start"
        exercise(ports["http"], ports["scpi"])
        assert process.poll() is None, "Server exited during integration checks"
        print("HTTP/SCPI integration passed: static assets, typed operations, frames, ownership, input limits")
    except Exception:
        print("Server startup:", startup)
        while not lines.empty():
            print(lines.get_nowait())
        raise
    finally:
        # 清理进程不等同于验证优雅关闭，尤其是在 Windows 上。
        # 等待操作完成、关闭射频和回收线程的顺序由受控 C++ 生命周期测试验证。
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
