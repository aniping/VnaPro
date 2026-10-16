"""模拟校准诊断的显式系数模型与帧校验；不读取文件或控制服务。"""

import math


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
