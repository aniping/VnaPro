"""软件夹具/模拟工具使用的独立 VNBCAL02 编解码；不读取旧 JSON 校准文件。"""

import base64
import math
import struct


MAGIC = b"VNBCAL02"
BYTE_LIMIT = 8 * 1024 * 1024
METHODS = {"onePort": (1, 6, "onePortTerms"), "twoPort": (2, 24, "twoPortTerms"),
           "fourPort": (4, 96, "fourPortTerms")}


def checksum(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def encode_calibration(value):
    """从内存中的声明夹具生成自包含 .vcal；id=0仅表示待导入，线格式使用占位ID1。"""
    method, components, field = METHODS[value["method"]]
    axis, power = value["frequencyHz"], value["powerDbm"]
    points = len(axis)
    rows = value[field]
    name, identity = value["name"].encode("utf-8"), value["deviceIdentity"].encode("utf-8")
    if not 2 <= points <= 10001 or len(power) != points or len(rows) != points:
        raise ValueError("Calibration dimensions differ")
    if len(name) > 128 or not 0 < len(identity) <= 512:
        raise ValueError("Calibration metadata length is invalid")
    payload = bytearray(struct.pack(
        "<Q8Id", int(value.get("id", "0")) or 1, method, int(value["includeIsolation"]),
        value["port1"], value["port2"], points, len(name), len(identity), 0, value["ifBandwidthHz"],
    ))
    payload.extend(name + identity)
    payload.extend(struct.pack("<" + "d" * points, *axis))
    payload.extend(struct.pack("<" + "d" * points, *power))
    for row in rows:
        if len(row) != components or not all(math.isfinite(number) for number in row):
            raise ValueError("Calibration coefficient row is invalid")
        payload.extend(struct.pack("<" + "d" * components, *row))
    result = MAGIC + struct.pack("<IIQQ", 2, 0, len(payload), checksum(payload)) + payload
    if len(result) > BYTE_LIMIT:
        raise ValueError("Calibration binary exceeds its byte limit")
    return result


def download_bytes(envelope):
    if envelope["format"] != "binary" or envelope["encoding"] != "base64":
        raise ValueError("Expected a binary download envelope")
    raw = base64.b64decode(envelope["content"], validate=True)
    if len(raw) > BYTE_LIMIT or ("byteLength" in envelope and len(raw) != envelope["byteLength"]):
        raise ValueError("Binary download length differs")
    return raw


def decode_calibration(raw):
    """仅接受当前自有二进制版本，供下载完整性与实际数据比较。"""
    if not 80 <= len(raw) <= BYTE_LIMIT or raw[:8] != MAGIC:
        raise ValueError("Invalid calibration binary signature or length")
    version, flags, length, digest = struct.unpack_from("<IIQQ", raw, 8)
    if version != 2 or flags or length != len(raw) - 32 or checksum(raw[32:]) != digest:
        raise ValueError("Invalid calibration binary header/checksum")
    identity_id, method, isolation, port1, port2, points, name_count, identity_count, reserved, ifbw = struct.unpack_from(
        "<Q8Id", raw, 32
    )
    choices = {entry[0]: (name, entry[1], entry[2]) for name, entry in METHODS.items()}
    if method not in choices or isolation > 1 or reserved or not 2 <= points <= 10001:
        raise ValueError("Invalid calibration binary model/dimensions")
    method_name, components, field = choices[method]
    if length != 48 + name_count + identity_count + points * (components + 2) * 8:
        raise ValueError("Calibration binary payload shape differs")
    offset = 80
    name = raw[offset:offset + name_count].decode("utf-8")
    offset += name_count
    identity = raw[offset:offset + identity_count].decode("utf-8")
    offset += identity_count
    axis = list(struct.unpack_from("<" + "d" * points, raw, offset))
    offset += points * 8
    power = list(struct.unpack_from("<" + "d" * points, raw, offset))
    offset += points * 8
    rows = []
    for _ in range(points):
        rows.append(list(struct.unpack_from("<" + "d" * components, raw, offset)))
        offset += components * 8
    if not all(math.isfinite(number) for number in [ifbw, *axis, *power]) or not all(
        math.isfinite(number) for row in rows for number in row
    ):
        raise ValueError("Nonfinite calibration binary value")
    result = {"id": str(identity_id), "name": name, "deviceIdentity": identity,
              "method": method_name, "port1": port1, "port2": port2,
              "includeIsolation": bool(isolation), "ifBandwidthHz": ifbw,
              "frequencyHz": axis, "powerDbm": power, "onePortTerms": [], "twoPortTerms": []}
    result[field] = rows
    return result


def file_fields(value):
    return {"format": "binary", "encoding": "base64",
            "content": base64.b64encode(encode_calibration(value)).decode("ascii")}
