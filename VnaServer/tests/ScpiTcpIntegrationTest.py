"""验证真实 TCP 服务、消息分帧和模拟设备的测量生命周期。"""
import math
import os
import queue
import socket
import subprocess
from pathlib import Path
import sys
import threading
import time

from server_startup import isolated_server, listening_port


class Client:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.socket.settimeout(5)
        self.buffer = b""

    def close(self):
        self.socket.close()

    def write(self, line):
        self.socket.sendall((line + "\n").encode("ascii"))

    def read(self):
        while b"\n" not in self.buffer:
            block = self.socket.recv(8192)
            if not block:
                raise AssertionError("Controller connection closed unexpectedly")
            self.buffer += block
        result, self.buffer = self.buffer.split(b"\n", 1)
        return result.rstrip(b"\r").decode("ascii")

    def query(self, line):
        self.write(line)
        return self.read()


def values(response):
    result = [float(value) for value in response.split(",")]
    assert all(math.isfinite(value) for value in result), "Non-finite mock data"
    return result


def connect_controller(port):
    # 断开的控制连接可能仍在等待已受理操作完成后释放控制权。重试连接，
    # 身份查询也允许观察者执行；再原值写回 ESE，确认当前连接持有写控制权。
    deadline = time.monotonic() + 5
    while True:
        client = Client(port)
        try:
            assert client.query("*IDN?").startswith("VnaPro,")
            event_enable = int(client.query("*ESE?"))
            assert client.query(f"*ESE {event_enable};*OPC?") == "+1"
            return client
        except (OSError, AssertionError):
            client.close()
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.01)


def expect_closed(client, line):
    """观察者的写入、破坏查询与非法消息被整行拒绝并关闭连接。"""
    try:
        client.write(line)
        assert client.socket.recv(8192) == b"", "Rejected observer received a partial program response"
    except ConnectionError:
        pass


def exercise_observers(port, writer):
    """真实多连接共享显示状态；观察者不能消费 owner 错误或自动升级。"""
    observers = []
    try:
        for _ in range(7):
            observer = Client(port)
            assert observer.query("*IDN?").startswith("VnaPro,")
            observers.append(observer)
        # 控制者与七个观察者达到八连接上限，第九条连接必须拒绝。
        extra = Client(port)
        try:
            expect_closed(extra, "*IDN?")
        finally:
            extra.close()
        writer.write("CALC:FORM REAL")
        assert writer.query("CALC:FORM?") == "REAL"
        for observer in observers:
            assert observer.query("CALC:FORM?") == "REAL"
            assert observer.query("*OPC?") == "+1"
        # 错误计数允许观察，读清 ESR 与错误出队必须保持 writer 独占。
        writer.write("*CLS;:INVALID_WRITER_COMMAND")
        assert writer.query("SYST:ERR:COUN?") == "1"
        assert observers[0].query("SYST:ERR:COUN?") == "1"
        expect_closed(observers[0], "*IDN?;:CALC:FORM PHAS")
        expect_closed(observers[1], "*STB?;*ESR?")
        expect_closed(observers[2], "SYST:ERR?")
        expect_closed(observers[3], "*IDN?;:NOT_IMPLEMENTED?")
        assert writer.query("SYST:ERR:COUN?") == "1", "Observers polluted or consumed owner errors"
        assert writer.query("CALC:FORM?") == "REAL", "Rejected mixed message changed shared format"
        assert writer.query("SYST:ERR?").startswith("-113,")
        assert writer.query("SYST:ERR?").startswith("0,")
        # 合法观察连接断开也不能释放真实 writer。
        observers[4].close()
        assert writer.query("CALC:FORM IMAG;FORM?") == "IMAG"
        assert observers[5].query("CALC:FORM?") == "IMAG"
    finally:
        for observer in observers:
            observer.close()


def exercise(port):
    client = connect_controller(port)
    try:
        # 写命令不返回确认消息；后续查询产生唯一的响应。
        assert int(client.query("*CLS;:SENS1:SWE:MODE HOLD;*OPC?")) == 1
        assert int(client.query("*IDN?;*OPC?").rsplit(";", 1)[1]) == 1
        # 验证任意分包边界和 CRLF 结束符，以及随后粘连的两条消息。
        client.socket.sendall(b"SENSe1:FREQuency:STA")
        client.socket.sendall(b"Rt 1MHZ\r\nSENS1:FREQ:STOP 100MHZ\nSENS1:SWE:POIN 17\n")
        assert float(client.query("SENS1:SWE:POIN?")) == 17
        assert values(client.query("SENS1:X?")) == [1e6 + index * 99e6 / 16 for index in range(17)]
        client.write('CALC1:PAR:DEF:EXT "Wave,A","a1,1"')
        client.write('CALC1:PAR:DEF:EXT "Wave,B","b1,1"')
        catalog = client.query("CALC1:PAR:CAT:EXT?")
        assert "Wave,A" in catalog and "Wave,B" in catalog
        client.write("FORM:DATA ASC,0")
        client.write("TRIG:SOUR IMM")
        assert int(client.query("SENS1:SWE:MODE SING;*OPC?")) == 1
        assert client.query("SENS1:SWE:MODE?") == "HOLD"
        assert client.query("SYST:ERR?").startswith("0,")
        client.write('CALC1:PAR:SEL "Wave,A"')
        a = values(client.query("CALC1:DATA? RDATA"))
        client.write('CALC1:PAR:SEL "Wave,B"')
        b = values(client.query("CALC1:DATA? RDATA"))
        assert len(a) == len(b) == 34 and a != b
        client.write('CALC1:PAR:SEL "Wave,A"')
        assert values(client.query("CALC1:DATA? RDATA")) == a, "Selection must preserve the complete frame"
        # 手动触发的 CONT 运行保持待触发状态；INIT 请求一次有限的逻辑扫描。
        client.write("TRIG:SOUR MAN")
        client.write("SENS1:SWE:MODE CONT")
        assert int(client.query("INIT1:IMM;*OPC?")) == 1
        assert client.query("SENS1:SWE:MODE?") == "CONT"
        assert client.query("INIT:CONT?") == "0"
        assert int(client.query("SENS1:SWE:MODE HOLD;*OPC?")) == 1
        # 验证标准状态与错误状态，以及重新连接后仍保留的仪器错误。
        client.write("*CLS")
        client.write("*ESE 32")
        client.write("THISCOMMANDDOESNOTEXIST")
        assert int(client.query("*ESR?")) & 32
        assert client.query("*ESR?") == "0"
        assert client.query("SYST:ERR:COUN?") == "1"
        client.close()
        client = connect_controller(port)
        assert client.query("SYST:ERR?").startswith("-113,")
        assert client.query("SYST:ERR?").startswith("0,")
        exercise_observers(port, client)
        # 相对命令头沿用当前路径；公共命令不改变该路径。
        relative = client.query("SENS1:FREQ:STAR?;*OPC?;STOP?").split(";")
        assert [float(value) for value in relative] == [1000000, 1, 100000000]
        client.write("ABOR")
        assert client.query("SYST:ERR?").startswith("-113,")
        # 连接断开不会丢弃此前已受理的有限扫描。
        client.write("SENS1:SWE:POIN 19")
        assert float(client.query("SENS1:SWE:POIN?")) == 19
        client.write("TRIG:SOUR IMM")
        client.write("SENS1:SWE:MODE SING;*OPC?")
        client.close()
        client = connect_controller(port)
        assert client.query("SENS1:SWE:MODE?") == "HOLD"
        assert len(values(client.query("CALC1:DATA? RDATA"))) == 38
        # 完整的大数组仍遵守 ASCII 响应的大小限制。
        client.write("SENS1:SWE:POIN 10001")
        assert int(client.query("SENS1:SWE:MODE SING;*OPC?")) == 1
        assert len(values(client.query("SENS1:X?"))) == 10001
        assert len(values(client.query("CALC1:DATA? RDATA"))) == 20002
        client.write("SENS1:SWE:POIN 10002")
        assert client.query("SYST:ERR?").startswith("-222,")
        assert float(client.query("SENS1:SWE:POIN?")) == 10001
    finally:
        client.close()


def main():
    environment = dict(os.environ, VNAPRO_SIM_MODE="golden")
    environment.pop("VNAPRO_SIM_SEED", None)
    with isolated_server(
        sys.argv[1], sys.argv[2], environment,
        config=Path(__file__).with_name("golden_test.yml"), board="tdc1vn08", web_enabled=False,
    ) as server:
        process = subprocess.Popen(
            [os.fspath(server.executable)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", env=server.environment,
        )
        lines = queue.Queue()

        def collect():
            for line in process.stdout:
                lines.put(line.rstrip())
            lines.put(None)

        collector = threading.Thread(target=collect, daemon=True)
        collector.start()
        startup = []
        try:
            deadline = time.monotonic() + 10
            while True:
                line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
                assert line is not None, "Server exited before the SCPI listener became ready"
                startup.append(line)
                listener = listening_port(line)
                if listener is not None and listener[0] == "scpi":
                    port = listener[1]
                    break
                if time.monotonic() >= deadline:
                    raise AssertionError("Server did not start")
            exercise(port)
            print("TCP SCPI integration passed: framing, completion, snapshots, ownership, limits")
        except Exception:
            print("Server output:", startup)
            while not lines.empty():
                print(lines.get_nowait())
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
