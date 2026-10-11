"""从旧启动输出或当前日志中提取服务实际监听端口。"""

import re


LISTENER_PATTERNS = (
    ("scpi", re.compile(r"(?:^LISTENING\s+|TCP SCPI 已监听，address=)\S+:(\d+)(?=\s|$)")),
    ("http", re.compile(r"(?:^WEB_LISTENING\s+|Web 已监听，address=)\S+:(\d+)(?=\s|$)")),
)
COLOR_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")


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
