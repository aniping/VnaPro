"""验证 VN10 默认启动、双/四端口完整采集和 VN08 显式兼容入口。"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from GoldenIqIntegrationTest import running_server
from InstrumentConfigurationIntegrationTest import (
    assert_frame, define_supported, scpi_rejections, status_defaults,
)
from WebHttpIntegrationTest import HttpClient, connect_scpi, identity_number


def continuous_display(http, client):
    assert int(client.query("SENS1:SWE:MODE CONT;*OPC?")) == 1
    first = http.frame()
    deadline = time.monotonic() + 5
    while True:
        frame = http.frame()
        if identity_number(frame["frameSeq"]) > identity_number(first["frameSeq"]):
            break
        assert time.monotonic() < deadline, "VN10 连续模式没有发布新的完整帧"
        time.sleep(0.01)
    run_id = http.status()["runId"]
    # 显示操作经过 C++，不重新启动正在运行的单板扫描。
    assert int(client.query('CALC1:PAR:SEL "Trc1";:CALC1:FORM PHAS;*OPC?')) == 1
    assert int(client.query("DISP:WIND1:TRAC1:Y:PDIV 20;RLEV 0;*OPC?")) == 1
    assert int(client.query("CALC1:MARK1:STAT ON;X 4005000000;*OPC?")) == 1
    status = http.status()
    assert status["runId"] == run_id
    assert status["display"]["selectedMeasurement"] == "Trc1"
    assert client.query("CALC1:FORM?") == "PHAS"
    assert float(client.query("DISP:WIND1:TRAC1:Y:PDIV?")) == 20
    assert float(client.query("CALC1:MARK1:X?")) == 4005000000
    assert int(client.query("SENS1:SWE:MODE HOLD;*OPC?")) == 1
    stopped = http.frame()
    time.sleep(0.06)
    assert http.frame()["frameSeq"] == stopped["frameSeq"], "HOLD 未形成完整帧屏障"
    assert client.query("SYST:ERR?").startswith("0,"), "新驱动验收留下仪器错误"


def exercise(config, port_count, driver, board, mode):
    environment = dict(os.environ, VNAPRO_SIM_MODE=mode, VNAPRO_SIM_SEED="42")
    with running_server(sys.argv[1], driver, environment, config=config, board=board) as ports:
        http = HttpClient(ports["http"])
        status_defaults(http, port_count)
        client = connect_scpi(ports["scpi"])
        try:
            assert_frame(http, client)
            scpi_rejections(http, client, port_count)
            define_supported(http, client, port_count)
            continuous_display(http, client)
        finally:
            client.close()


def wrong_abi(config, board, driver):
    environment = dict(os.environ, VNAPRO_SIM_MODE="golden")
    environment.pop("VNAPRO_SIM_SEED", None)
    result = subprocess.run(
        [sys.argv[1], "--board", board, "--driver", os.path.abspath(driver),
         "--config", str(config), "--no-web", "--port", "0"],
        capture_output=True, text=True, encoding="utf-8", env=environment, timeout=8,
    )
    assert result.returncode == 1, (board, result.returncode, result.stdout, result.stderr)
    assert "symbol missing" in result.stdout + result.stderr, result


def main():
    assert len(sys.argv) == 4, "usage: BoardDriverIntegrationTest.py <server> <VN10 driver> <VN08 driver>"
    with tempfile.TemporaryDirectory(prefix="vnapro-board-") as temporary:
        config = Path(temporary) / "four-port.yml"
        config.write_text("log: {file: {enabled: false}}\n", encoding="utf-8")
        # 不传 --board 或 --driver，实测服务默认路径确实选择 VN10。
        exercise(config, 4, None, None, "noise")
        exercise(config, 4, sys.argv[2], "tdc1vn10", "golden")
        two_port = Path(temporary) / "two-port.yml"
        two_port.write_text("instrument: {portCount: 2}\nlog: {file: {enabled: false}}\n", encoding="utf-8")
        exercise(two_port, 2, sys.argv[2], "tdc1vn10", "noise")
        exercise(config, 4, None, "tdc1vn08", "golden")
        # 选错接口时先完成必需符号绑定校验，不调用任何同名异签名函数。
        wrong_abi(config, "tdc1vn10", sys.argv[3])
        wrong_abi(config, "tdc1vn08", sys.argv[2])
        invalid = subprocess.run([sys.argv[1], "--board", "unknown"], capture_output=True, timeout=5)
        assert invalid.returncode == 2, invalid
    print("VN10 默认/显式启动、双/四端口、共享显示、HOLD 及 VN08 兼容验收通过")


if __name__ == "__main__":
    main()
