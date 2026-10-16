"""校准文件的真实 HTTP 往返与落盘格式验收；只操作调用方私有服务。"""

import base64
import json

from CalibrationIntegrationTest import action
from vnb_calibration_file import decode_calibration, download_bytes
from WebHttpIntegrationTest import accepted, wait_no_owner, wait_operation


def renew(http, token):
    claim = http.request("POST", "/api/v1/control/claim", {"sessionId": token}, token)
    assert claim["sessionId"] == token and claim["hasControl"], claim


def pool(http):
    return [
        (entry["id"], entry["name"]) for entry in http.status()["calibration"]["sets"]
    ]


def check_binary_manifest(bundle, expected_ids, store_directory, manifest_name):
    store = bundle.executable.parent / store_directory
    manifest = json.loads((store / manifest_name).read_bytes())
    assert manifest["version"] == 2 and manifest["channelId"] == 1
    assert [row["id"] for row in manifest["sets"]] == expected_ids
    for row in manifest["sets"]:
        payload = (store / row["file"]).resolve()
        assert payload.parent == store.resolve() and payload.suffix == ".vcal"
        with payload.open("rb") as stream:
            assert stream.read(12) == b"VNBCAL02\x02\x00\x00\x00", row["file"]
    return manifest


def download_file(http, set_id, format_name):
    envelope = http.request(
        "GET", "/api/v1/calibration/sets/" + set_id + "?format=" + format_name
    )
    assert envelope["format"] == format_name
    assert envelope["encoding"] == ("base64" if format_name == "binary" else "utf8")
    content = (
        base64.b64decode(envelope["content"], validate=True)
        if format_name == "binary"
        else envelope["content"].encode("utf-8")
    )
    assert len(content) == envelope["byteLength"]
    assert envelope["filename"].endswith(".vcal" if format_name == "binary" else ".csv")
    if format_name == "binary":
        assert content.startswith(b"VNBCAL02\x02\x00\x00\x00")
    return envelope


def reject_file_replays(http, token, body, after):
    format_name = body["format"]
    conflict = dict(body, content=body["content"] + "changed")
    http.request("POST", "/api/v1/calibration", conflict, token, expected=409)
    stale = dict(body, requestId="file-stale-" + format_name)
    http.request("POST", "/api/v1/calibration", stale, token, expected=409)
    malformed = action(
        http, "importFile", "file-malformed-" + format_name,
        format=format_name, encoding=body["encoding"], content="!",
    )
    http.request("POST", "/api/v1/calibration", malformed, token, expected=400)
    assert http.status()["calibration"] == after, "文件失败不得更改池、选择或启用状态"


def roundtrip_file(http, token, format_name, original):
    envelope = download_file(http, original["id"], format_name)
    before = http.status()["calibration"]
    body = action(
        http, "importFile", "file-import-" + format_name,
        **{field: envelope[field] for field in ("format", "encoding", "content")},
    )
    http.request("POST", "/api/v1/calibration", body, expected=403)
    assert http.status()["calibration"] == before
    operation = accepted(http, "calibration", body, token)
    wait_operation(http, operation, token)
    assert accepted(http, "calibration", body, token) == operation
    after = http.status()["calibration"]
    assert len(after["sets"]) == len(before["sets"]) + 1
    assert all(after[field] == before[field] for field in ("selectedSetId", "enabled", "compatible"))
    reject_file_replays(http, token, body, after)
    imported_id = after["sets"][-1]["id"]
    restored = decode_calibration(download_bytes(http.request("GET", "/api/v1/calibration/sets/" + imported_id)))
    restored["id"] = original["id"]
    assert restored == original, "二进制/CSV 经真实 HTTP 导入后默认二进制导出逐值一致"
    return imported_id


def file_roundtrips(http, set_id, mutate):
    """真实 keep-alive HTTP 往返；文件导入仍遵守所有权、幂等与 CAS。"""
    wait_no_owner(http)
    token = http.request("POST", "/api/v1/control/claim", {})["sessionId"]
    original = decode_calibration(download_bytes(http.request("GET", "/api/v1/calibration/sets/" + set_id)))
    assert original["id"] == set_id
    baseline = pool(http)
    try:
        for format_name in ("binary", "csv"):
            renew(http, token)
            imported_id = roundtrip_file(http, token, format_name, original)
            mutate(http, token, "delete", "file-delete-" + format_name, setId=imported_id)
            assert pool(http) == baseline, "临时传输验收不得改变原204条池容量"
    finally:
        http.request("POST", "/api/v1/control/release", {}, token)
    wait_no_owner(http)
