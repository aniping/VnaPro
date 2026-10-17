"""使用真实服务进程验证日志配置、采集摘要和滚动文件。"""

import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path

from server_startup import isolated_server, listening_ports


def environment(mode="golden"):
    result = dict(os.environ, VNAPRO_SIM_MODE=mode)
    result.pop("VNAPRO_SIM_SEED", None)
    return result


def config_file(directory, extra=""):
    path = directory / "vnapro.yml"
    path.write_text(
        "log:\n  level: TRACE\n  console: {enabled: false}\n  file:\n"
        "    path: logs/service.log\n    maxSizeBytes: 1024\n    maxFiles: 2\n"
        "    maxRecordBytes: 512\n  flush: {level: TRACE}\n" + extra,
        encoding="utf-8",
    )
    return path


def logs(directory):
    return sorted((directory / "logs").glob("service*.log"))


def contents(directory):
    deadline = time.monotonic() + 1
    while True:
        try:
            return "".join(path.read_text(encoding="utf-8") for path in logs(directory))
        except (FileNotFoundError, PermissionError):
            # Windows 轮转可在枚举和读取之间更名或暂时占用文件，重取完整快照。
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.01)


@contextmanager
def running(config):
    with isolated_server(
        sys.argv[1],
        sys.argv[2],
        environment(),
        config=config,
        web_enabled=False,
        directory=config.parent,
    ) as bundle:
        with listening_server(bundle) as port:
            yield port


@contextmanager
def listening_server(bundle):
    process = subprocess.Popen(
        [bundle.executable],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        env=bundle.environment,
    )
    try:
        deadline = time.monotonic() + 10
        while True:
            assert process.poll() is None, "服务在完成监听前退出"
            # 控制台关闭时从实际文件判断就绪，不能临时打开控制台绕过配置验收。
            ports = listening_ports(contents(bundle.config.parent))
            if "scpi" in ports:
                yield ports["scpi"]
                break
            assert time.monotonic() < deadline, "日志中未找到就绪的 SCPI 监听端口"
            time.sleep(0.01)
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        output = process.stdout.read()
        errors = process.stderr.read()
        process.stdout.close()
        process.stderr.close()
        assert not output, output
        assert not errors, errors


def query(connection, reader, message):
    connection.sendall((message + "\n").encode("ascii"))
    response = reader.readline()
    assert response, "SCPI 响应意外关闭"
    return response.decode("ascii").strip()


def assert_measurement_logs(output):
    """逐帧日志只保证在当前滚动窗口内存在，必须在后续命令前验收。"""
    trace = r"\[TRACE\] \[\d+\] VnaCore - .*channel=1\b.*frame=1\b.*points=51\b"
    assert re.search(trace, output), output
    assert "VnaService - 扫描模式已提交" in output, output


def assert_rotated_logs(directory):
    """验收有界保留窗口，不能要求已被正常轮转淘汰的历史采集日志仍在。"""
    output = contents(directory)
    assert "VnaCore" not in output and "frame=" not in output, output
    assert "SCPI event=received" in output and "program=*IDN?" in output, output
    completed = r"SCPI event=completed .*path=\*IDN query=1 channel=0 code=0"
    assert re.search(completed, output), output
    paths = logs(directory)
    assert len(paths) == 3, paths
    assert all(0 < path.stat().st_size <= 1024 for path in paths), paths
    assert not (directory / "logs" / "service.3.log").exists()
    prefix = re.compile(
        r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} "
        r"\[(TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)\] \[\d+\] Vna\w+ - "
    )
    assert all(prefix.match(line) for line in output.splitlines()), output


def exercise_logging(directory):
    config = config_file(directory)
    with running(config) as port:
        # 路径按配置文件目录解析，而非调用者当前目录。
        before = contents(directory)
        assert "设备初始化成功" in before, before
        assert "TCP SCPI 已监听" in before, before
        with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
            with connection.makefile("rb") as reader:
                assert query(connection, reader, "*IDN?").startswith(
                    "VnaPro,NativeSimulator,"
                )
                assert (
                    int(query(connection, reader, "*CLS;SENS1:SWE:MODE HOLD;*OPC?"))
                    == 1
                )
                for index in range(35):
                    assert (
                        int(
                            query(
                                connection, reader, f"SENS1:SWE:POIN {17 + index};*OPC?"
                            )
                        )
                        == 1
                    )
                assert (
                    int(
                        query(
                            connection,
                            reader,
                            "TRIG:SOUR IMM;:SENS1:SWE:MODE SING;*OPC?",
                        )
                    )
                    == 1
                )
                # 新增命令日志也参与 3 KiB 滚动预算；在后续命令覆盖前检查实际逐帧记录。
                assert_measurement_logs(contents(directory))
                assert int(query(connection, reader, "SENS1:SWE:MODE HOLD;*OPC?")) == 1
                assert query(connection, reader, "SYST:ERR?").startswith("0,")
                # 固定追加正常查询，确定性地覆盖旧采集记录，验证滚动窗口的真实语义。
                for _ in range(8):
                    assert query(connection, reader, "*IDN?").startswith(
                        "VnaPro,NativeSimulator,"
                    )
                # HOLD 且控制连接保持打开时读取；关闭连接的日志仍可能触发下一次轮转。
                assert_rotated_logs(directory)


def exercise_unicode_path(directory):
    config = directory / "vnapro.yml"
    config.write_text(
        "log:\n  console: {enabled: false}\n  file: {path: 日志/采集.log}\n",
        encoding="utf-8",
    )
    with isolated_server(
        sys.argv[1],
        sys.argv[2],
        environment(),
        config=config,
        web_enabled=False,
        directory=directory,
    ) as bundle:
        text = bundle.config.read_text(encoding="utf-8")
        bundle.config.write_text(
            text.replace(Path(sys.argv[2]).name, "缺失驱动.dll"), encoding="utf-8"
        )
        failed = subprocess.run(
            [bundle.executable],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env=bundle.environment,
            timeout=10,
        )
        assert failed.returncode == 1, failed.stderr
    output = (directory / "日志" / "采集.log").read_text(encoding="utf-8")
    assert "缺失驱动.dll" in output and "服务退出" in output, output


def failed_start(config, mode="golden", transform=None):
    """在真实配置目录启动失败场景，保留日志相对路径语义。"""
    with isolated_server(
        sys.argv[1],
        sys.argv[2],
        environment(mode),
        config=config,
        web_enabled=False,
        directory=config.parent,
    ) as bundle:
        if transform is not None:
            transform(bundle.config)
        return subprocess.run(
            [bundle.executable],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env=bundle.environment,
            timeout=10,
        )


def exercise_startup_failures(directory):
    config = config_file(directory, "unknown: true\n")
    invalid = failed_start(config)
    assert invalid.returncode == 2, invalid
    assert "unknown" in invalid.stderr, invalid.stderr
    assert not listening_ports(invalid.stdout + invalid.stderr)
    assert not logs(directory), "无效配置应在设备启动和创建日志目标前拒绝"
    config = config_file(directory)
    invalid_mode = failed_start(config, mode="invalid")
    assert (
        invalid_mode.returncode == 2 and "driver.simulator.mode" in invalid_mode.stderr
    ), invalid_mode
    assert not logs(directory), "非法噪声配置必须在驱动和日志初始化前拒绝"
    invalid_utf8 = failed_start(
        config, transform=lambda path: path.write_bytes(b"log: \xff\n")
    )
    assert invalid_utf8.returncode == 2, invalid_utf8
    assert not listening_ports(invalid_utf8.stdout + invalid_utf8.stderr)
    assert not logs(directory), "非法编码配置必须在创建日志前拒绝"
    config = config_file(directory)

    # 合法配置引用缺失驱动，加载失败和退出日志应在进程结束前刷盘。
    def missing_driver(path):
        text = path.read_text(encoding="utf-8")
        path.write_text(
            text.replace(Path(sys.argv[2]).name, "missing-driver.dll"), encoding="utf-8"
        )

    failed = failed_start(config, transform=missing_driver)
    assert failed.returncode == 1, failed
    output = contents(directory)
    assert "[ERROR]" in output and "missing-driver.dll" in output, output
    assert "服务退出，exit_code=1" in output, output
    assert not listening_ports(failed.stdout + output)


def main():
    with tempfile.TemporaryDirectory(prefix="vnapro-logging-") as temporary:
        directory = Path(temporary)
        valid = directory / "valid"
        invalid = directory / "invalid"
        valid.mkdir()
        invalid.mkdir()
        exercise_logging(valid)
        exercise_startup_failures(invalid)
        unicode_path = directory / "中文目录"
        unicode_path.mkdir()
        exercise_unicode_path(unicode_path)
    print("日志集成验证通过：配置目录、采集摘要、轮转保留、启动失败与刷新")


if __name__ == "__main__":
    main()
