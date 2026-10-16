"""隔离真实 TCP 验证默认 INFO 的 SCPI 诊断，不连接用户正在运行的服务。"""

import re
import sys
import tempfile
import time
from pathlib import Path

from LoggingIntegrationTest import contents, running
from ScpiTcpIntegrationTest import Client, expect_closed


def command(client, text):
    assert client.query(text + ";*OPC?") == "+1", text


def assert_logged(directory, predicate, reason):
    deadline = time.monotonic() + 3
    while True:
        text = contents(directory)
        if predicate(text):
            return text
        assert time.monotonic() < deadline, reason + "\n" + text[-8000:]
        time.sleep(0.01)


def exercise(directory):
    config = directory / "vnapro.yml"
    config.write_text(
        "log:\n  level: INFO\n  console: {enabled: false}\n"
        "  file: {path: logs/service.log, maxSizeBytes: 4194304, maxFiles: 1}\n"
        "  flush: {level: INFO}\n",
        encoding="utf-8",
    )
    with running(config) as port:
        client = Client(port)
        try:
            assert client.query("*IDN?").startswith("VnaPro,NativeSimulator,")
            command(client, "*CLS;:SENS1:SWE:MODE HOLD")
            # 一个 TCP 分片不能伪装成一条完整命令，也不能被重复记录。
            prefix = "SENS1:SWE:PO"
            client.socket.sendall(prefix.encode("ascii"))
            time.sleep(0.03)
            assert prefix not in contents(directory)
            client.socket.sendall(b"IN 3\r\n*OPC?\n")
            assert client.read() == "+1"
            command(client, "INIT:CONT OFF;:INIT:IMM")
            payload = client.query("SENS1:X?")
            assert len(payload.split(",")) == 3
            command(client, "NOT_A_COMMAND")
            assert client.query("SYST:ERR?").startswith("-113,")
            assert client.query("SYST:ERR?").startswith("0,"), (
                "日志不能消费或重复添加错误"
            )
            command(client, "DISP:WIND1:TRAC1:FEED 'MissingForLogTest'")
            assert not client.query("SYST:ERR?").startswith("0,"), "服务 future 应失败"
            assert client.query("SYST:ERR?").startswith("0,")
            observer = Client(port)
            try:
                assert observer.query("*IDN?").startswith("VnaPro,")
                expect_closed(observer, "*IDN?;*CLS")
            finally:
                observer.close()
            oversized = Client(port)
            try:
                expect_closed(oversized, "A" * (16 * 1024 + 1))
            finally:
                oversized.close()
            observers = []
            try:
                for _ in range(7):
                    observer = Client(port)
                    observers.append(observer)
                    assert observer.query("*IDN?").startswith("VnaPro,")
                extra = Client(port)
                try:
                    expect_closed(extra, "*IDN?")
                finally:
                    extra.close()
            finally:
                for observer in observers:
                    observer.close()
            text = assert_logged(
                directory,
                lambda output: (
                    "reason=line_budget" in output
                    and "reason=connection_budget" in output
                ),
                "超限消息与连接必须记录传输拒绝原因",
            )
            received = [line for line in text.splitlines() if "event=received" in line]
            assert sum("program=SENS1:SWE:POIN 3" in line for line in received) == 1
            for event in (
                "event=completed",
                "event=failed",
                "event=rejected",
                "event=tx_complete",
            ):
                assert event in text, event
            assert "code=-113" in text and "MissingForLogTest" in text
            assert payload not in text, "不能把数值响应正文写入日志"
            assert all(
                "connection=" in line
                for line in text.splitlines()
                if "SCPI event=" in line
            )
            observer_rx = next(
                line for line in received if "program=*IDN?;*CLS" in line
            )
            connection = re.search(r"connection=(\d+)", observer_rx).group(1)
            assert any(
                "event=disconnected" in line
                and f"connection={connection} " in line
                and "reason=message_rejected" in line
                for line in text.splitlines()
            ), "观察者拒绝与断线必须可通过同一连接编号关联"
        finally:
            client.close()


def main():
    assert len(sys.argv) == 3, (
        "usage: ScpiLoggingIntegrationTest.py <server> <simulator>"
    )
    with tempfile.TemporaryDirectory(prefix="vnapro-scpi-audit-") as temporary:
        exercise(Path(temporary))
    print(
        "SCPI INFO audit passed: fragmented RX, execution, errors, observer and transport rejection"
    )


if __name__ == "__main__":
    main()
