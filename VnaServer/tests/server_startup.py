"""隔离配置驱动的服务进程，并从实际日志中提取监听端口。"""

from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from types import SimpleNamespace


LISTENER_PATTERNS = (
    ("scpi", re.compile(r"(?:^LISTENING\s+|TCP SCPI 已监听，address=)\S+:(\d+)(?=\s|$)")),
    ("http", re.compile(r"(?:^WEB_LISTENING\s+|Web 已监听，address=)\S+:(\d+)(?=\s|$)")),
)
COLOR_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")


def link_or_copy(source, destination):
    """优先共享只读二进制，配置与日志始终由独立测试目录持有。"""
    source, destination = Path(source), Path(destination)
    if destination.exists():
        return destination
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)
    return destination


def replace_section(text, name, section):
    """仅替换测试要控制的顶层配置，保留原始仪器和日志设置。"""
    pattern = rf"(?ms)^{re.escape(name)}:[^\n]*(?:\n(?:[ \t]+[^\n]*|[ \t]*|#[^\n]*))*(?=\n\S|\Z)"
    return re.sub(pattern, "", text).strip() + "\n" + section


def write_startup_config(destination, original, driver=None, board="tdc1vn08",
                         mode="golden", seed=42, web_enabled=True, noise_amplitude=0.002):
    """网络、驱动和噪声均写入正式 YAML，不通过启动参数或环境覆盖。"""
    server = {
        "ip": "127.0.0.1", "scpiPort": 0, "webPort": 0, "webEnabled": web_enabled,
    }
    text = replace_section(original, "server", "server: " + json.dumps(server) + "\n")
    settings = {"type": "simulator", "simulator": {"mode": mode, "seed": int(seed),
                              "noiseAmplitude": float(noise_amplitude)}}
    if board is not None:
        settings["board"] = board
    if driver is not None:
        settings["library"] = Path(driver).name
    text = replace_section(text, "driver", "driver: " + json.dumps(settings) + "\n")
    destination.write_text(text, encoding="utf-8")


@contextmanager
def isolated_server(server, driver, environment, config=None, board="tdc1vn08",
                    web_enabled=True, web_root=None, directory=None, noise_amplitude=None):
    """准备无参数启动的独立程序目录，不修改真实构建或发布目录。"""
    executable = Path(server).resolve()
    config_source = Path(config) if config is not None else executable.parent / "vnapro.yml"
    original = config_source.read_text(encoding="utf-8")
    clean_environment = dict(environment)
    mode = clean_environment.pop("VNAPRO_SIM_MODE", "golden")
    seed = clean_environment.pop("VNAPRO_SIM_SEED", "42")
    inherited_amplitude = clean_environment.pop("VNAPRO_SIM_NOISE_AMPLITUDE", "0.002")
    if noise_amplitude is None:
        noise_amplitude = inherited_amplitude
    with tempfile.TemporaryDirectory(prefix="vnapro-server-") as temporary:
        destination = Path(directory) if directory is not None else Path(temporary)
        destination.mkdir(parents=True, exist_ok=True)
        packaged_executable = destination / executable.name
        link_or_copy(executable, packaged_executable)
        for path in executable.parent.iterdir():
            if path.is_file() and (path.suffix.lower() in (".dll", ".dylib") or ".so" in path.name):
                link_or_copy(path, destination / path.name)
        if driver is not None:
            source_driver = Path(driver).resolve()
            link_or_copy(source_driver, destination / source_driver.name)
        if web_enabled:
            assets = (Path(web_root) if web_root is not None else
                      Path(__file__).resolve().parents[2] / "WebUI" / "dist")
            shutil.copytree(assets, destination / "WebUI", copy_function=link_or_copy, dirs_exist_ok=True)
        configuration = destination / "vnapro.yml"
        write_startup_config(configuration, original, driver, board, mode, seed, web_enabled,
                             noise_amplitude)
        yield SimpleNamespace(executable=packaged_executable, config=configuration,
                              environment=clean_environment)


def listening_port(line):
    """返回监听类型与端口；普通日志、尚未绑定的零端口或非法端口不表示就绪。"""
    plain = COLOR_ESCAPE.sub("", line).strip()
    for kind, pattern in LISTENER_PATTERNS:
        match = pattern.search(plain)
        if match is not None:
            port = int(match.group(1))
            if 1 <= port <= 65535:
                return kind, port
    return None


def listening_ports(output):
    """从多行日志快照收集已经报告的监听端口，支持日志文件和合并的控制台输出。"""
    result = {}
    for line in output.splitlines():
        listener = listening_port(line)
        if listener is not None:
            result[listener[0]] = listener[1]
    return result
