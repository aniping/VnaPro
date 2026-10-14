"""实测无参数启动的固定端口、分开绑定、中文页面目录和 Web 开关。"""

from http.client import HTTPConnection
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from server_startup import isolated_server


def free_port():
    with socket.socket() as connection:
        connection.bind(("0.0.0.0", 0))
        return connection.getsockname()[1]


def identity(port):
    with socket.create_connection(("127.0.0.2", port), timeout=0.5) as connection:
        connection.sendall(b"*IDN?\n")
        with connection.makefile("rb") as stream:
            return stream.readline().decode("ascii").strip()


def exercise(bundle, cwd, web_enabled):
    scpi_port, web_port = free_port(), free_port()
    while web_port == scpi_port:
        web_port = free_port()
    server = {
        "ip": "0.0.0.0", "scpiIp": "127.0.0.2", "webIp": "127.0.0.1",
        "scpiPort": scpi_port, "webPort": web_port, "webEnabled": web_enabled,
        "webRoot": "页面 目录" if web_enabled else "不存在的目录",
    }
    configuration = {
        "server": server,
        "driver": {"type": "simulator", "board": "tdc1vn10", "library": Path(sys.argv[2]).name,
                   "simulator": {"mode": "noise", "seed": 18446744073709551615}},
        "instrument": {"sweep": {"defaultSweepMode": "HOLD"}},
        "log": {"file": {"enabled": False}},
    }
    bundle.config.write_text(json.dumps(configuration, ensure_ascii=False), encoding="utf-8")
    environment = dict(bundle.environment, VNAPRO_SIM_MODE="invalid", VNAPRO_SIM_SEED="invalid")
    output = cwd.parent / ("web-on.log" if web_enabled else "web-off.log")
    with output.open("wb") as stream:
        process = subprocess.Popen([bundle.executable], cwd=cwd, env=environment,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            while True:
                assert process.poll() is None, output.read_text(encoding="utf-8")
                try:
                    assert identity(scpi_port).startswith("VnaPro,NativeSimulator,")
                    break
                except OSError:
                    assert time.monotonic() < deadline, "配置指定的 SCPI 地址和端口没有启动"
                    time.sleep(0.01)
            if web_enabled:
                connection = HTTPConnection("127.0.0.1", web_port, timeout=2)
                try:
                    connection.request("GET", "/")
                    response = connection.getresponse()
                    assert response.status == 200 and response.read() == b"configured WebUI root"
                finally:
                    connection.close()
            else:
                with socket.socket() as connection:
                    assert connection.connect_ex(("127.0.0.1", web_port)) != 0, "关闭 Web 后仍监听端口"
            assert "seed=18446744073709551615" in output.read_text(encoding="utf-8")
            assert not list(cwd.iterdir()), "程序错误地使用当前工作目录读取或写入资源"
        finally:
            process.terminate()
            process.wait(timeout=5)


def main():
    with isolated_server(sys.argv[1], sys.argv[2], dict(os.environ), board="tdc1vn10") as bundle:
        page = bundle.executable.parent / "页面 目录"
        page.mkdir()
        (page / "index.html").write_bytes(b"configured WebUI root")
        with tempfile.TemporaryDirectory(prefix="vnapro-startup-cwd-") as temporary:
            cwd = Path(temporary) / "unrelated"
            cwd.mkdir()
            exercise(bundle, cwd, True)
            exercise(bundle, cwd, False)
    print("配置固定端口、独立 IP、中文 Web 根目录、Web 开关及最大种子验收通过")


if __name__ == "__main__":
    main()
