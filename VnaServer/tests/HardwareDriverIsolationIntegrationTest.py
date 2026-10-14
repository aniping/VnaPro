"""以测试 ABI 探针验证硬件启动隔离；不声称验证真实板卡连接或采集。"""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from ScpiTcpIntegrationTest import Client
from server_startup import link_or_copy, listening_ports, replace_section


IDENTITY = "Huawei,HardwareEnvironmentProbe,4PORT,TEST"
SENTINELS = {
    "VNAPRO_SIM_MODE": "hardware-probe-mode-unchanged",
    "VNAPRO_SIM_SEED": "hardware-probe-seed-unchanged",
    "VNAPRO_SIM_NOISE_AMPLITUDE": "hardware-probe-amplitude-unchanged",
}


def prepare(directory, server, driver, simulator, explicit_library=True):
    """保留原模板的仪器能力，仅替换隔离启动相关节点并改为 HOLD。"""
    directory.mkdir()
    executable = link_or_copy(server, directory / server.name)
    for path in server.parent.iterdir():
        if path.is_file() and (path.suffix.lower() in (".dll", ".dylib") or ".so" in path.name):
            link_or_copy(path, directory / path.name)
    link_or_copy(driver, directory / driver.name)
    configuration = (server.parent / "vnapro.yml").read_text(encoding="utf-8")
    server_settings = {"ip": "127.0.0.1", "scpiPort": 0, "webPort": 0,
                       "webEnabled": False, "webRoot": "missing-web-assets"}
    driver_settings = {"type": "hardware", "board": "tdc1vn08", "simulator": simulator}
    if explicit_library:
        driver_settings["library"] = driver.name
    for name, settings in (("server", server_settings), ("driver", driver_settings),
                           ("log", {"level": "INFO", "console": {"enabled": True},
                                    "file": {"enabled": False}})):
        configuration = replace_section(configuration, name, name + ": " + json.dumps(settings) + "\n")
    configuration, count = re.subn(r"(?m)^([ \t]+defaultSweepMode:[ \t]*)\S+",
                                   r"\g<1>HOLD", configuration, count=1)
    assert count == 1, "启动模板缺少默认扫描模式，无法确保硬件探针仅处于 HOLD"
    (directory / "vnapro.yml").write_text(configuration, encoding="utf-8")
    return executable


def exercise(directory, server, driver, simulator, fail_initialization=False, missing_library=False):
    executable = prepare(directory, server, driver, simulator, explicit_library=not missing_library)
    environment = dict(os.environ, **SENTINELS)
    # 测试专用变量只控制探针返回值，与正式服务配置没有关联。
    environment.pop("VNAPRO_TEST_HW_INIT_FAILURE", None)
    if fail_initialization:
        environment["VNAPRO_TEST_HW_INIT_FAILURE"] = "1"
    unrelated = directory / "unrelated-cwd"
    unrelated.mkdir()
    output = directory / "startup.log"
    with output.open("wb") as stream:
        process = subprocess.Popen([executable], cwd=unrelated, env=environment,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            if fail_initialization or missing_library:
                expected_exit = 2 if missing_library else 1
                assert process.wait(timeout=8) == expected_exit, "驱动配置错误与初始化故障的退出码未正确区分"
                text = output.read_text(encoding="utf-8")
                if missing_library:
                    assert "driver.library" in text, "硬件缺少库名时误加载了构建默认模拟库"
                else:
                    assert "DrvHwInit returned 17" in text, text
                assert not listening_ports(text), "驱动初始化失败后仍然接收连接"
            else:
                deadline = time.monotonic() + 8
                while True:
                    text = output.read_text(encoding="utf-8")
                    assert process.poll() is None, text
                    ports = listening_ports(text)
                    if "scpi" in ports:
                        break
                    assert time.monotonic() < deadline, text
                    time.sleep(0.01)
                assert set(ports) == {"scpi"}, text
                client = Client(ports["scpi"])
                try:
                    assert client.query("*IDN?") == IDENTITY, "未读取硬件探针的真实驱动标识"
                    assert client.query("SENS1:SWE:MODE?") == "HOLD", "测试意外触发硬件采集"
                    assert client.query("*OPC?") == "+1"
                    assert client.query("SYST:ERR?").startswith("0,"), "硬件启动留下仪器错误"
                finally:
                    client.close()
            assert "模拟驱动启动配置" not in output.read_text(encoding="utf-8"), \
                "硬件路径仍然执行了模拟环境配置"
            assert not list(unrelated.iterdir()), "启动过程错误依赖当前工作目录"
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


def main():
    assert len(sys.argv) == 3, "usage: HardwareDriverIsolationIntegrationTest.py <server> <test ABI probe>"
    server, driver = (Path(argument).resolve() for argument in sys.argv[1:])
    malformed = {"mode": "noise", "seed": 42, "noiseAmplitude": "not-a-number"}
    with tempfile.TemporaryDirectory(prefix="vnapro-hardware-isolation-") as temporary:
        directory = Path(temporary)
        exercise(directory / "malformed-amplitude", server, driver, malformed)
        exercise(directory / "simulator-scalar", server, driver, "not-a-map")
        exercise(directory / "initialization-failure", server, driver, malformed, fail_initialization=True)
        exercise(directory / "missing-library", server, driver, malformed, missing_library=True)
    print("硬件 ABI 探针验收通过：忽略模拟配置、保留原环境、身份/HOLD 查询、故障报告及缺少库名保护")


if __name__ == "__main__":
    main()
