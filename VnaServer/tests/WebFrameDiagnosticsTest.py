"""使用真实服务验证帧诊断的阶段关联、默认关闭和敏感字段隔离。"""

from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from server_startup import isolated_server, listening_ports
from WebHttpIntegrationTest import HttpClient, connect_scpi


@contextmanager
def server(bundle, log_path):
    process = subprocess.Popen([bundle.executable], stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, env=bundle.environment)
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            assert process.poll() is None, "诊断服务在监听前退出"
            output = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
            ports = listening_ports(output)
            if "http" in ports and "scpi" in ports:
                yield ports
                return
            time.sleep(0.01)
        raise AssertionError("诊断服务监听超时")
    finally:
        # 终止仅清理此独立测试进程，不作为驱动有序关闭的证据。
        process.terminate()
        process.wait(timeout=5)


def verify(level):
    with tempfile.TemporaryDirectory(prefix="vnapro-frame-log-") as temporary:
        directory = Path(temporary)
        config = directory / "source.yml"
        log_path = directory / "frames.log"
        config.write_text(
            f"log:\n  level: {level}\n  console: {{enabled: false}}\n"
            f"  file: {{path: {json.dumps(str(log_path))}, maxSizeBytes: 1048576}}\n"
            "  flush: {level: DEBUG}\n", encoding="utf-8")
        environment = dict(os.environ, VNAPRO_SIM_MODE="noise", VNAPRO_SIM_SEED="42")
        with isolated_server(sys.argv[1], sys.argv[2], environment, config=config,
                             board="tdc1vn10") as bundle:
            with server(bundle, log_path) as ports:
                client = connect_scpi(ports["scpi"])
                try:
                    assert client.query("SENS1:SWE:MODE HOLD;*OPC?") == "+1"
                    assert client.query("SENS1:SWE:POIN 4000;*OPC?") == "+1"
                    assert client.query("SENS1:SWE:MODE SING;*OPC?") == "+1"
                finally:
                    client.close()
                http = HttpClient(ports["http"])
                token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
                body, _ = http.request("GET", "/api/v1/frame?encoding=packed",
                                       token=token, raw=True)
                frame = json.loads(body)
                assert frame["originalPointCount"] == 4000 and not frame["decimated"]
                deadline = time.monotonic() + 2
                while True:
                    output = log_path.read_text(encoding="utf-8")
                    if level != "DEBUG" or "frame_send conn=" in output:
                        break
                    assert time.monotonic() < deadline, "发送完成诊断未写入"
                    time.sleep(0.01)
                assert token not in output, "控制令牌不能出现在日志里"
                assert frame["traces"][0]["real"]["data"][:80] not in output, "不能打印整帧 IQ"
                if level == "INFO":
                    assert "frame_build conn=" not in output
                    return
                matches = re.findall(r"frame_build conn=(\d+) frameSeq=(\d+) runId=(\d+) "
                                     r"displayRevision=(\d+) points=4000 traces=1 buildUs=(\d+)", output)
                assert matches, output
                connection, sequence, run, display, _ = matches[-1]
                assert (sequence, run, display) == (frame["frameSeq"], frame["runId"], frame["displayRevision"])
                assert re.search(rf"frame_encode conn={connection} frameSeq={sequence} bytes={len(body)} "
                                 r"encodeCpuUs=\d+ encodeWallUs=\d+", output), output
                assert re.search(rf"frame_send conn={connection} frameSeq={sequence} runId={run} "
                                 rf"displayRevision={display} bytes={len(body)} sendUs=\d+ writeBlocked=\d+", output), output


if __name__ == "__main__":
    assert len(sys.argv) == 3
    verify("DEBUG")
    verify("INFO")
