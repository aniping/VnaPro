"""Exercise the real TCP service, framing and the mock measurement lifecycle."""
import math
import os
import queue
import socket
import subprocess
import sys
import threading
import time


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
    # A disconnected controller may be releasing accepted operations. Retry
    # the transport until a complete identity response proves control was granted.
    deadline = time.monotonic() + 5
    while True:
        client = Client(port)
        try:
            assert client.query("*IDN?").startswith("VnaPro,")
            return client
        except (OSError, AssertionError):
            client.close()
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.01)


def exercise(port):
    client = connect_controller(port)
    try:
        # No ACK is sent for writes; the following query is the sole response.
        assert int(client.query("*CLS;:SENS1:SWE:MODE HOLD;*OPC?")) == 1
        assert int(client.query("*IDN?;*OPC?").rsplit(";", 1)[1]) == 1
        # Arbitrary packet boundaries and CRLF, followed by two coalesced messages.
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
        # A manual CONT run stays armed; INIT requests one finite logical sweep.
        client.write("TRIG:SOUR MAN")
        client.write("SENS1:SWE:MODE CONT")
        assert int(client.query("INIT1:IMM;*OPC?")) == 1
        assert client.query("SENS1:SWE:MODE?") == "CONT"
        assert client.query("INIT:CONT?") == "0"
        assert int(client.query("SENS1:SWE:MODE HOLD;*OPC?")) == 1
        # Standard status/error state, and instrument errors survive reconnect.
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
        # A second connection receives no control or device state changes.
        second = Client(port)
        try:
            try:
                second.write("*IDN?")
                closed = second.socket.recv(32)
                assert closed == b"", "A second controller must be rejected"
            except ConnectionError:
                pass
        finally:
            second.close()
        # Relative headers follow the current path; common commands preserve it.
        relative = client.query("SENS1:FREQ:STAR?;*OPC?;STOP?").split(";")
        assert [float(value) for value in relative] == [1000000, 1, 100000000]
        client.write("ABOR")
        assert client.query("SYST:ERR?").startswith("-113,")
        # Disconnect does not discard a finite sweep that was accepted earlier.
        client.write("SENS1:SWE:POIN 19")
        assert float(client.query("SENS1:SWE:POIN?")) == 19
        client.write("TRIG:SOUR IMM")
        client.write("SENS1:SWE:MODE SING;*OPC?")
        client.close()
        client = connect_controller(port)
        assert client.query("SENS1:SWE:MODE?") == "HOLD"
        assert len(values(client.query("CALC1:DATA? RDATA"))) == 38
        # Large whole arrays stay within the bounded ASCII response contract.
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
    process = subprocess.Popen(
        [sys.argv[1], "--port", "0", "--driver", os.path.abspath(sys.argv[2])],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    lines = queue.Queue()

    def collect():
        for line in process.stdout:
            lines.put(line.rstrip())

    collector = threading.Thread(target=collect, daemon=True)
    collector.start()
    startup = []
    try:
        deadline = time.monotonic() + 10
        while True:
            line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
            startup.append(line)
            if line.startswith("LISTENING "):
                port = int(line.rsplit(":", 1)[1])
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
