#!/usr/bin/env python3
"""仅软件联调：向正在运行的 NativeSimulator 导入明确的非单位诊断校准集。

这不是通过 OSM/THRU 物理标准求得的 TOSM 校准，不证明硬件精度。
脚本不启动服务、不编辑启动配置、不扩大频率或端口能力。它暂时添加缺少的
S 参数定义，完成一次真实扫描，随后清理临时定义并释放控制权；成功时保留
新诊断集为已选中/启用，扫描停在 HOLD。导出文件来自实际服务的 Export。
match 明确取零，以独立逐元素 (M-offset)/tracking 核对真实校正输出；
非零 match 的联合求逆由数学/服务回归验证，此脚本不重复实现生产公式。

示例：python scripts/vnb_simulation_calibration.py --http-url http://127.0.0.1:8080
      --name SIMULATION_ONLY_FOUR_PORT_DIAGNOSTIC --output reference/simulation-calibration.json
"""

import argparse
import json
import math
import sys
import time
import uuid
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import quote, urlencode, urlsplit
from urllib.request import Request, urlopen


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def finite(value):
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


def reject_constant(value):
    raise ValueError("服务返回非有限 JSON 数值：" + value)


class Http:
    def __init__(self, url):
        parsed = urlsplit(url)
        require(
            parsed.scheme in ("http", "https")
            and parsed.hostname
            and not parsed.query
            and not parsed.fragment
            and not parsed.username
            and not parsed.password,
            "--http-url 必须是无凭据、无查询字符串的 HTTP(S) 服务地址",
        )
        self.url = url.rstrip("/")
        self.token = None

    def request(self, method, path, body=None, expected=200):
        headers = {"Accept": "application/json"}
        if self.token:
            headers["X-Vna-Session"] = self.token
        payload = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(
                body, ensure_ascii=False, allow_nan=False, separators=(",", ":")
            ).encode("utf-8")
        try:
            with urlopen(
                Request(self.url + path, data=payload, headers=headers, method=method),
                timeout=20,
            ) as response:
                data = response.read()
                require(
                    response.status == expected,
                    f"{method} {path} 返回 {response.status}",
                )
                require(
                    response.headers.get_content_type() == "application/json",
                    "服务响应不是 JSON",
                )
                return json.loads(data.decode("utf-8"), parse_constant=reject_constant)
        except HTTPError as error:
            with error:
                detail = error.read(800).decode("utf-8", errors="replace")
            raise RuntimeError(
                f"{method} {path} 返回 HTTP {error.code}：{detail}"
            ) from error

    def status(self):
        return self.request("GET", "/api/v1/status")

    def operation(self, method, path, body):
        body = dict(body, requestId="simulation-fixture-" + uuid.uuid4().hex)
        accepted = self.request(method, path, body, expected=202)
        operation_id = accepted["operationId"]
        deadline = time.monotonic() + 120
        renew_at = time.monotonic() + 5
        while True:
            result = self.request(
                "GET", "/api/v1/operations/" + quote(operation_id, safe="")
            )
            if result["state"] != "pending":
                require(
                    result["state"] == "completed"
                    and result.get("result", {}).get("ok") is True,
                    "服务操作失败：" + json.dumps(result, ensure_ascii=False),
                )
                return
            require(time.monotonic() < deadline, "等待真实服务操作完成超时")
            if time.monotonic() >= renew_at:
                self.request("POST", "/api/v1/control/claim", {"sessionId": self.token})
                renew_at = time.monotonic() + 5
            time.sleep(0.02)

    def calibration(self, channel):
        return self.request(
            "GET", "/api/v1/calibration?" + urlencode({"channelId": channel})
        )

    def change_calibration(self, channel, action, **fields):
        body = dict(
            fields,
            action=action,
            channelId=channel,
            expectedRevision=self.calibration(channel)["revision"],
        )
        self.operation("POST", "/api/v1/calibration", body)

    def frame(self, channel, names):
        query = urlencode(
            [("channelId", channel)] + [("trace", name) for name in names]
        )
        return self.request("GET", "/api/v1/frame?" + query)


def checked_status(http):
    status = http.status()
    identity = status.get("identity", "")
    require(
        status.get("backend") == "NativeSimulator"
        and isinstance(identity, str)
        and "NativeSimulator" in identity,
        "仅允许明确标识 NativeSimulator 的软件服务；HardwareDriver 或未知身份一律拒绝",
    )
    capabilities = status.get("capabilities", {})
    require(capabilities.get("portCount") == 4, "当前启动配置必须真实提供四个端口")
    require(
        finite(capabilities.get("minFrequencyHz"))
        and finite(capabilities.get("maxFrequencyHz"))
        and 0 <= capabilities["minFrequencyHz"] < capabilities["maxFrequencyHz"],
        "服务回读的实际频率能力无效；脚本不会扩大或夹紧能力",
    )
    require(
        status.get("activeConfigValid") and not status.get("pending"),
        "当前配置无效或仍有待完成操作",
    )
    require(
        status.get("state") in ("ready", "running", "Ready", "Running"), "服务尚未就绪"
    )
    config = status["config"]
    require(
        finite(config.get("powerDbm")) and config["powerDbm"] == 0,
        "当前真实功率必须为 0 dBm",
    )
    require(
        finite(config.get("ifBandwidthHz")) and config["ifBandwidthHz"] > 0,
        "当前 IFBW 无效",
    )
    return status


def coefficients(cell):
    offset = complex(0.002 * (cell + 1), -0.001 * (cell + 1))
    tracking = complex(1.2 + 0.04 * cell, 0.01 * (cell + 1))
    return offset, tracking


def document(name, status, axis):
    config = status["config"]
    require(
        len(axis) == config["points"]
        and len(axis) >= 2
        and all(finite(hz) and hz == int(hz) for hz in axis)
        and all(axis[index - 1] < axis[index] for index in range(1, len(axis))),
        "实际帧轴必须是完整、递增的整数 Hz 轴",
    )
    capabilities = status["capabilities"]
    require(
        axis[0] == config["startHz"]
        and axis[-1] == config["stopHz"]
        and capabilities["minFrequencyHz"]
        <= axis[0]
        <= axis[-1]
        <= capabilities["maxFrequencyHz"],
        "实际帧频轴与当前启动配置或服务回读能力不一致",
    )
    row = []
    for cell in range(16):
        offset, tracking = coefficients(cell)
        row.extend((offset.real, offset.imag, 0.0, 0.0, tracking.real, tracking.imag))
    return {
        "schema": 3,
        "id": "0",
        "name": name,
        "deviceIdentity": status["identity"],
        "method": "fourPort",
        "port1": 1,
        "port2": 4,
        "includeIsolation": True,
        "frequencyHz": axis,
        "ifBandwidthHz": config["ifBandwidthHz"],
        "powerDbm": [0.0] * len(axis),
        "onePortTerms": [],
        "twoPortTerms": [],
        "fourPortTerms": [list(row) for _ in axis],
    }


def ordered_names(http, status, channel, temporary):
    names = []
    for receiver in range(1, 5):
        for source in range(1, 5):
            parameter = f"S{receiver}{source}"
            existing = next(
                (
                    item
                    for item in status["measurements"]
                    if item["channelId"] == channel
                    and item.get("parameter") == parameter
                ),
                None,
            )
            if existing:
                names.append(existing["name"])
                continue
            name = "SimFixture_" + uuid.uuid4().hex[:12] + "_" + parameter
            temporary.append(name)
            http.operation(
                "POST",
                "/api/v1/measurements",
                {
                    "name": name,
                    "parameter": parameter,
                    "channelId": channel,
                    "expectedRevision": http.status()["revision"],
                },
            )
            names.append(name)
    return names


def traces(frame, names, calibrated):
    require(
        frame.get("available") and frame.get("current") and not frame.get("decimated"),
        "必须读取当前完整已完成帧",
    )
    by_name = {trace["name"]: trace for trace in frame["traces"]}
    for name in names:
        trace = by_name.get(name, {})
        require(
            trace.get("available")
            and trace.get("calibration") == calibrated
            and len(trace.get("real", [])) == len(frame["frequencyHz"])
            and len(trace.get("imag", [])) == len(frame["frequencyHz"])
            and len(trace.get("valid", [])) == len(frame["frequencyHz"])
            and all(trace["valid"])
            and all(finite(value) for value in trace["real"] + trace["imag"]),
            "迹线缺失、校准标签错误或含无效样本：" + name,
        )
    return by_name


def run(args):
    http = Http(args.http_url)
    initial = checked_status(http)
    channel = initial["activeChannelId"]
    require(
        any(item["channelId"] == channel for item in initial["measurements"]),
        "当前通道至少需要一个已有测量，以便安全清理临时定义",
    )
    before_cal = http.calibration(channel)
    require(
        not before_cal["collecting"] and len(before_cal["sets"]) < 256,
        "校准会话仍在采集或存储池已满",
    )
    require(
        not any(item["name"] == args.name for item in before_cal["sets"]),
        "同名校准集已存在，请换 --name",
    )
    temporary, imported_id, success = [], None, False
    cleanup_errors = []
    try:
        http.token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
        checked_status(http)
        http.operation(
            "POST", "/api/v1/operations", {"action": "hold", "channelId": channel}
        )
        http.change_calibration(channel, "enable", enabled=False)
        names = ordered_names(http, initial, channel, temporary)
        http.operation(
            "POST", "/api/v1/operations", {"action": "single", "channelId": channel}
        )
        raw_frame = http.frame(channel, names)
        raw = traces(raw_frame, names, "uncalibrated")
        current = checked_status(http)
        data = document(args.name, current, raw_frame["frequencyHz"])
        known_ids = {item["id"] for item in before_cal["sets"]}
        http.change_calibration(channel, "import", data=data)
        added = [
            item
            for item in http.calibration(channel)["sets"]
            if item["id"] not in known_ids
        ]
        require(
            len(added) == 1 and added[0]["name"] == args.name,
            "服务未生成唯一的目标导入集",
        )
        imported_id = added[0]["id"]
        http.change_calibration(channel, "select", setId=imported_id)
        http.change_calibration(channel, "enable", enabled=True)
        corrected_frame = http.frame(channel, names)
        require(
            all(
                corrected_frame[key] == raw_frame[key]
                for key in ("channelId", "runId", "revision", "frameSeq")
            ),
            "原始与校正读取跨越了不同的采集帧",
        )
        corrected = traces(corrected_frame, names, "calibrated")
        maximum_error = 0.0
        for cell, name in enumerate(names):
            offset, tracking = coefficients(cell)
            for index, (real, imag) in enumerate(
                zip(raw[name]["real"], raw[name]["imag"])
            ):
                expected = (complex(real, imag) - offset) / tracking
                actual = complex(
                    corrected[name]["real"][index], corrected[name]["imag"][index]
                )
                error = abs(actual - expected)
                require(
                    error <= 1e-10 * max(1.0, abs(expected)),
                    f"真实校正不符合独立期望：{name}, point={index}",
                )
                maximum_error = max(maximum_error, error)
        exported = http.request(
            "GET",
            "/api/v1/calibration/sets/"
            + quote(imported_id, safe="")
            + "?"
            + urlencode({"channelId": channel}),
        )
        require(
            exported == dict(data, id=imported_id),
            "服务实际导出与已验证的导入数据不一致",
        )
        output = Path(args.output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(
            json.dumps(exported, ensure_ascii=False, allow_nan=False, indent=2) + "\n",
            encoding="utf-8",
        )
        success = True
        print(
            json.dumps(
                {
                    "validation": "simulation_only",
                    "name": args.name,
                    "setId": imported_id,
                    "channelId": channel,
                    "points": len(data["frequencyHz"]),
                    "verifiedTraces": 16,
                    "maximumError": maximum_error,
                    "export": str(output),
                },
                ensure_ascii=False,
            )
        )
    finally:
        if http.token:
            if not success:
                try:
                    if before_cal["selectedSetId"] != "0":
                        http.change_calibration(
                            channel, "select", setId=before_cal["selectedSetId"]
                        )
                    if imported_id:
                        http.change_calibration(channel, "delete", setId=imported_id)
                    http.change_calibration(
                        channel, "enable", enabled=before_cal["enabled"]
                    )
                except Exception as error:  # noqa: BLE001 - 清理失败须记录并继续释放其他资源。
                    cleanup_errors.append("恢复校准状态失败：" + str(error))
            for name in reversed(temporary):
                try:
                    status = http.status()
                    if any(item["name"] == name for item in status["measurements"]):
                        http.operation(
                            "DELETE",
                            "/api/v1/measurements/" + quote(name, safe=""),
                            {
                                "channelId": channel,
                                "expectedRevision": status["revision"],
                            },
                        )
                except Exception as error:  # noqa: BLE001 - 一条迹线清理失败不能阻止释放控制权。
                    cleanup_errors.append("清理临时迹线失败：" + str(error))
            try:
                http.request("POST", "/api/v1/control/release", {})
            except Exception as error:  # noqa: BLE001 - 保留原始失败，汇总报告控制权释放错误。
                cleanup_errors.append("释放控制权失败：" + str(error))
        for message in cleanup_errors:
            print(message, file=sys.stderr)
    require(not cleanup_errors, "软件校验结束，但资源清理失败；请检查以上错误")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--http-url",
        required=True,
        help="正在运行的 VNB HTTP 服务，例如 http://127.0.0.1:8080",
    )
    parser.add_argument(
        "--name",
        default="SIMULATION_ONLY_FOUR_PORT_DIAGNOSTIC",
        help="明确的软件诊断集名称",
    )
    parser.add_argument(
        "--output", required=True, help="保存服务实际导出的 schema 3 JSON 文件"
    )
    args = parser.parse_args()
    require(
        args.name
        and len(args.name.encode("utf-8")) <= 128
        and not any(ord(char) < 32 or ord(char) == 127 for char in args.name),
        "--name 必须是 1..128 字节有效名称",
    )
    run(args)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print("软件联调失败：" + str(error), file=sys.stderr)
        sys.exit(1)
