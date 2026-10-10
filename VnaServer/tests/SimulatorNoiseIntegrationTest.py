"""Repeatable changing device data across Native/Core/service/HTTP/SCPI."""

from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
import os
import sys
import threading
import time

from WebHttpIntegrationTest import HttpClient, accepted, connect_scpi, wait_operation
from GoldenIqIntegrationTest import running_server


def run_device(seed, exercise):
    env = dict(os.environ, VNAPRO_SIM_MODE="noise", VNAPRO_SIM_SEED=str(seed))
    with running_server(sys.argv[1], sys.argv[2], env) as ports:
        return exercise(HttpClient(ports["http"]), ports["scpi"])


def configure(http):
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    patch = {"expectedRevision": http.status()["revision"], "requestId": "configure-noise",
             "startHz": 2000000, "stopHz": 18000000, "points": 65, "ifBandwidthHz": 1000}
    wait_operation(http, accepted(http, "config", patch, token), token)
    for port in (2, 3, 4):
        measurement = {"name": "NoiseB" + str(port), "receiverKind": "b", "receiverPort": port,
                       "sourcePort": port, "expectedRevision": http.status()["revision"],
                       "requestId": "define-noise-" + str(port)}
        wait_operation(http, accepted(http, "measurements", measurement, token), token)
    return token


def check_frame(frame, revision, run=None):
    assert frame["available"] and frame["current"], frame
    assert frame["revision"] == revision, frame
    if run is not None:
        assert frame["runId"] == run, frame
    assert frame["originalPointCount"] == frame["displayPointCount"] == 65
    assert not frame["decimated"]
    assert frame["frequencyHz"] == [2000000 + point * 250000 for point in range(65)]
    assert [trace["sourcePort"] for trace in frame["traces"]] == [1, 2, 3, 4]
    for trace in frame["traces"]:
        assert trace["available"] and len(trace["real"]) == len(trace["imag"]) == len(trace["valid"]) == 65
        assert all(trace["valid"])
        assert all(math.isfinite(value) for value in trace["real"] + trace["imag"])


def data_signature(frame):
    # Device samples only; run/frame metadata is expected to change.
    return tuple(tuple(trace[key]) for trace in frame["traces"] for key in ("real", "imag"))


def exercise(http, scpi_port, pressure=False):
    token = configure(http)
    revision = http.status()["revision"]
    scans = []
    for scan in range(3):
        wait_operation(http, accepted(http, "operations",
            {"action": "single", "requestId": "noise-single-" + str(scan)}, token), token)
        frame = http.frame()
        check_frame(frame, revision)
        assert http.frame() == frame, "HOLD reads must not add fresh noise"
        scans.append(data_signature(frame))
    assert scans[0] != scans[1] != scans[2] and scans[0] != scans[2], "Noise must change between acquisitions"
    if pressure:
        wait_operation(http, accepted(http, "operations",
            {"action": "continuous", "requestId": "noise-continuous"}, token), token)
        run = http.status()["runId"]
        signatures = {}
        lock = threading.Lock()

        def reader():
            seen = set()
            previous = 0
            deadline = time.monotonic() + 10
            while len(seen) < 20:
                frame = http.frame()
                if frame.get("current") and frame.get("runId") == run:
                    check_frame(frame, revision, run)
                    seq = int(frame["frameSeq"])
                    assert seq >= previous, "Published frames must not move backwards"
                    previous = seq
                    digest = hashlib.sha256(json.dumps(frame, sort_keys=True).encode()).hexdigest()
                    with lock:
                        assert signatures.setdefault(seq, digest) == digest, "One frame identity cannot have two contents"
                    seen.add(seq)
                assert time.monotonic() < deadline, "Continuous acquisition stopped making progress"
                time.sleep(0.002)
            return len(seen)

        with ThreadPoolExecutor(max_workers=3) as readers:
            results = list(readers.map(lambda _: reader(), range(3)))
        assert results == [20, 20, 20], results
        wait_operation(http, accepted(http, "operations",
            {"action": "hold", "requestId": "noise-hold"}, token), token)
        print("Noise continuous: 3 readers x 20 distinct full frames; identities stable")
    held = http.frame()
    check_frame(held, revision)
    assert http.status()["config"]["sweepMode"] == "HOLD"
    assert http.frame() == held, "HOLD completes before further same-frame reads"
    http.request("POST", "/api/v1/control/release", {}, token)
    scpi = connect_scpi(scpi_port)
    try:
        for trace in held["traces"]:
            name = trace["name"].replace('"', '""')
            assert scpi.query('CALC1:PAR:SEL "' + name + '";*OPC?') == "+1"
            actual = [float(value) for value in scpi.query("CALC1:DATA? RDATA").split(",")]
            expected = [value for pair in zip(trace["real"], trace["imag"]) for value in pair]
            assert actual == expected, "SCPI must read exactly the held noisy IQ, without generating new noise"
        assert scpi.query("SYST:ERR?").startswith("0,"), "Noise must not cause data errors"
        assert http.frame() == held, "Protocol handover must retain the complete noisy frame"
    finally:
        scpi.close()
    return scans


def main():
    assert len(sys.argv) == 3, "usage: SimulatorNoiseIntegrationTest.py <server> <driver>"
    first = run_device(17, lambda http, port: exercise(http, port, pressure=True))
    replay = run_device(17, exercise)
    changed_seed = run_device(18, exercise)
    assert first == replay, "Fresh initialization with the same seed must replay all three full scans"
    assert first != changed_seed, "A different seed must change device samples"
    print("Noise integration passed: four sources, changing samples, seed replay, concurrent reads, HOLD and SCPI")


if __name__ == "__main__":
    main()
