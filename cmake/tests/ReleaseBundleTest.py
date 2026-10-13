"""将发布包复制到中文目录，验证无构建环境依赖的双板启动和 Web 资源。"""

from contextlib import contextmanager
from http.client import HTTPConnection
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from urllib.parse import quote


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def require_same_file(actual, expected):
    require(actual.is_file(), f"发布包缺少文件：{actual}")
    require(actual.read_bytes() == expected.read_bytes(), f"发布文件未同步最新内容：{actual}")


def verify_files(bundle, server_name, drivers, source_config, source_web):
    for name in (server_name, *drivers):
        path = bundle / name
        require(path.is_file() and path.stat().st_size > 0, f"发布包缺少程序或驱动：{path}")
    require((bundle / "vnapro.yml").is_file(), "发布包缺少可编辑的 vnapro.yml")
    require((bundle / ".vnapro-release-files").is_file(), "发布包缺少运行依赖清单")
    # 本地配置允许定制；示例模板必须在每次构建后同步源配置。
    require_same_file(bundle / "vnapro.example.yml", source_config)
    expected = {path.relative_to(source_web) for path in source_web.rglob("*") if path.is_file()}
    actual = {path.relative_to(bundle / "WebUI") for path in (bundle / "WebUI").rglob("*") if path.is_file()}
    require(Path("index.html") in expected, "验收需要已经构建的 WebUI/dist/index.html")
    require(expected == actual, f"发布 WebUI 文件集合与 dist 不同：缺失={expected - actual}，多余={actual - expected}")
    for relative in expected:
        require_same_file(bundle / "WebUI" / relative, source_web / relative)
    return expected


def isolated_environment():
    environment = dict(os.environ, VNAPRO_SIM_MODE="noise", VNAPRO_SIM_SEED="42")
    # 不允许从开发机的编译器 PATH 或额外动态库目录借用运行库。
    if os.name == "nt":
        system = Path(environment.get("SystemRoot", "C:/Windows"))
        environment["PATH"] = os.pathsep.join((str(system / "System32"), str(system)))
    else:
        environment["PATH"] = "/usr/bin:/bin"
    for key in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH"):
        environment.pop(key, None)
    return environment


def output_text(path):
    return path.read_bytes().decode("utf-8", errors="replace")


def stop_process(process):
    # 只关闭当前验收直接启动的进程，绝不查找或终止已有服务。
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


@contextmanager
def copied_server(server, cwd, output, environment, board=None):
    # 自定义配置可能关闭全部日志，直接选取空闲端口，不以日志输出作为启动条件。
    with socket.socket() as scpi, socket.socket() as http:
        scpi.bind(("127.0.0.1", 0))
        http.bind(("127.0.0.1", 0))
        ports = {"scpi": scpi.getsockname()[1], "http": http.getsockname()[1]}
    arguments = [str(server), "--port", str(ports["scpi"]), "--web-port", str(ports["http"])]
    if board is not None:
        arguments += ["--board", board]
    with output.open("wb") as stream:
        process = subprocess.Popen(arguments, cwd=cwd, env=environment, stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while True:
                text = output_text(output)
                require(process.poll() is None,
                        f"复制包启动失败，退出码={process.returncode}，输出：\n{text}")
                require(time.monotonic() < deadline, f"复制包监听启动超时，输出：\n{text}")
                try:
                    http_json(ports["http"], "/api/v1/status")
                    break
                except OSError:
                    pass
                time.sleep(0.01)
            yield ports, text
            require(process.poll() is None, f"验收期间服务退出：\n{output_text(output)}")
        finally:
            stop_process(process)


def http_get(port, path):
    connection = HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        connection.request("GET", path)
        response = connection.getresponse()
        data = response.read()
        require(response.status == 200, f"发布包 HTTP 读取失败：{path}，{response.status}，{data[:200]!r}")
        return data
    finally:
        connection.close()


def http_json(port, path):
    return json.loads(http_get(port, path).decode("utf-8"))


def verify_assets(port, bundle, files):
    require(http_get(port, "/") == (bundle / "WebUI/index.html").read_bytes(),
            "首页没有使用复制包内 WebUI/index.html")
    for relative in files:
        path = "/" + quote(relative.as_posix(), safe="/")
        require(http_get(port, path) == (bundle / "WebUI" / relative).read_bytes(),
                f"静态资源没有使用复制包内文件：{relative}")


def verify_scpi(port, identity):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
        connection.sendall(b"*IDN?\n")
        with connection.makefile("rb") as stream:
            require(stream.readline().decode("ascii").strip() == identity,
                    "复制包的 SCPI 身份与 HTTP 不一致")


def verify_acquisition(port, status):
    require(status["state"] != "Faulted", f"复制包采集初始化失败：{status}")
    if status["config"]["sweepMode"] != "CONT":
        return
    deadline, first = time.monotonic() + 5, None
    while True:
        frame = http_json(port, "/api/v1/frame")
        if frame["available"] and frame["current"]:
            require(frame["originalPointCount"] == status["config"]["points"], "复制包默认扫频点数未生效")
            require(frame["traces"] and all(trace["available"] for trace in frame["traces"]),
                    "复制包没有提供可用迹线")
            if first is not None and int(frame["frameSeq"]) > first:
                return
            first = int(frame["frameSeq"]) if first is None else first
        require(time.monotonic() < deadline, "复制包未发布连续完整帧")
        time.sleep(0.01)


def exercise(bundle, server_name, cwd, environment, files, label, board=None, custom=False):
    output = cwd.parent / (label + ".stdout.log")
    with copied_server(bundle / server_name, cwd, output, environment, board) as (ports, startup):
        expected_board = "TDC1VN10" if board is None else board.upper()
        if "board=" in startup:
            require("board=" + expected_board in startup, "复制包没有选择预期单板接口")
        status = http_json(ports["http"], "/api/v1/status")
        verify_scpi(ports["scpi"], status["identity"])
        verify_assets(ports["http"], bundle, files)
        verify_acquisition(ports["http"], status)
        if custom:
            require(status["capabilities"]["portCount"] == 2 and status["config"]["points"] == 17,
                    "服务没有加载复制包内修改后的仪器配置")
            require(status["config"]["sweepMode"] == "HOLD" and status["state"] == "Ready",
                    "复制包内启动模式配置没有生效")
            require(not http_json(ports["http"], "/api/v1/frame")["available"], "HOLD 启动不应自动产生帧")
    if custom:
        require((bundle / "logs/bundle-acceptance.log").is_file(), "相对日志路径没有落在复制包内")
    require(not list(cwd.iterdir()), "启动错误地依赖工作目录或向工作目录写入配置/日志")


def reject_missing_driver(bundle, server_name, driver_name, cwd, environment):
    driver = bundle / driver_name
    driver.rename(driver.with_name(driver.name + ".disabled"))
    output = cwd.parent / "missing-driver.stdout.log"
    with output.open("wb") as stream:
        process = subprocess.Popen([str(bundle / server_name), "--port", "0", "--web-port", "0"],
                                   cwd=cwd, env=environment, stdout=stream, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=8)
            require(process.returncode == 1,
                    f"包内驱动缺失应拒绝启动，退出码={process.returncode}：\n{output_text(output)}")
            require("Driver initialization failed" in output_text(output), "缺少驱动的启动诊断不明确")
        except subprocess.TimeoutExpired as error:
            raise AssertionError("包内驱动缺失仍然启动，可能回退到了原构建目录") from error
        finally:
            stop_process(process)


def main():
    require(len(sys.argv) == 7,
            "usage: ReleaseBundleTest.py <release> <server-name> <VN10-name> <VN08-name> <source-config> <WebUI-dist>")
    bundle, source_config, source_web = (Path(sys.argv[index]).resolve() for index in (1, 5, 6))
    files = verify_files(bundle, sys.argv[2], sys.argv[3:5], source_config, source_web)
    with tempfile.TemporaryDirectory(prefix="vnapro-release-") as temporary:
        root = Path(temporary)
        copied = root / "离线发布包 中文 空格"
        shutil.copytree(bundle, copied)
        cwd = root / "无关工作目录"
        cwd.mkdir()
        environment = isolated_environment()
        exercise(copied, sys.argv[2], cwd, environment, files, "default-vn10")
        # 只修改测试复制品，用非默认值证明配置来自包内且不依赖原构建路径。
        (copied / "vnapro.yml").write_text(
            "instrument: {portCount: 2, sweep: {defaultPoints: 17, defaultSweepMode: HOLD}}\n"
            "log: {level: INFO, console: {enabled: true}, file: {enabled: true, path: logs/bundle-acceptance.log}}\n",
            encoding="utf-8")
        exercise(copied, sys.argv[2], cwd, environment, files, "two-port-vn10", custom=True)
        exercise(copied, sys.argv[2], cwd, environment, files, "two-port-vn08", board="tdc1vn08", custom=True)
        reject_missing_driver(copied, sys.argv[2], sys.argv[3], cwd, environment)
    print("发布包离线复制、双板默认路径、配置生效、Web 资源及缺驱动拒绝启动验收通过")


if __name__ == "__main__":
    main()
