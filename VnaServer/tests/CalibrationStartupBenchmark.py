"""在临时部署及存储副本测启动；不操作原服务或原校准池。"""

import argparse
import hashlib
import json
import math
import os
import shutil
import statistics
import time
from pathlib import Path

from CalibrationPersistenceIntegrationTest import running
from server_startup import isolated_server, replace_section
from VnbScpiIntegrationTest import Client
from WebHttpIntegrationTest import HttpClient


def copy_pool(source, destination):
    """复制同一 manifest 引用的不可变文件；并发变更时拒绝混合快照。"""
    manifest = source / "channel-1.json"
    baseline = manifest.read_bytes()
    document = json.loads(baseline)
    destination.mkdir()
    for entry in document["sets"]:
        filename = entry["file"]
        original = (source / filename).resolve()
        if original.parent != source or Path(filename).name != filename:
            raise ValueError("校准文件引用必须位于源池目录")
        shutil.copyfile(original, destination / filename)
    if manifest.read_bytes() != baseline:
        raise RuntimeError("复制期间源校准池发生变化，请重新运行")
    (destination / manifest.name).write_bytes(baseline)
    return document, hashlib.sha256(baseline).hexdigest()


def benchmark_switches(ports, sets):
    """实际 TCP ACT + OPC + 错误查询；只切换副本中与当前刺激兼容的集。"""
    compatible = [entry for entry in sets if entry["compatible"]]
    samples = []
    client = Client(ports["scpi"])
    try:
        for entry in compatible:
            name = entry["name"].replace("'", "''")
            started = time.monotonic()
            client.socket.sendall((f"SENS1:CORR:CSET:ACT '{name}',0;*OPC?\n").encode())
            assert int(client.line()) == 1, "ACT 保存屏障未完成"
            assert client.query("SYST:ERR?").startswith("0,"), entry["name"]
            samples.append((time.monotonic() - started) * 1000)
        if compatible:
            assert int(client.query("SENS1:CORR?")) == 1
    finally:
        client.close()
    return {
        "compatibleSets": len(samples),
        "skippedIncompatibleSets": len(sets) - len(samples),
        "totalSeconds": sum(samples) / 1000,
        "firstMilliseconds": samples[0] if samples else None,
        "medianMilliseconds": statistics.median(samples) if samples else None,
        "p95Milliseconds": sorted(samples)[math.ceil(len(samples) * 0.95) - 1]
        if samples
        else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--store", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--max-seconds", type=float)
    parser.add_argument("--switch-all", action="store_true")
    options = parser.parse_args()
    if options.repeat < 1:
        parser.error("--repeat 必须为正整数")
    if options.max_seconds is not None and (
        not math.isfinite(options.max_seconds) or options.max_seconds <= 0
    ):
        parser.error("--max-seconds 必须为有限正数")
    samples = []
    switches = []
    with isolated_server(
        options.server,
        options.driver,
        os.environ,
        config=options.config,
        board="tdc1vn10",
    ) as bundle:
        log = {
            "level": "INFO",
            "console": {"enabled": True},
            "file": {"enabled": True, "path": "logs/calibration-benchmark.log"},
        }
        bundle.config.write_text(
            replace_section(
                bundle.config.read_text(encoding="utf-8"),
                "log",
                "log: " + json.dumps(log) + "\n",
            ),
            encoding="utf-8",
        )
        manifest, digest = copy_pool(
            options.store.resolve(), bundle.executable.parent / "calibration"
        )
        for _ in range(options.repeat):
            started = time.monotonic()
            with running(bundle) as ports:
                status = HttpClient(ports["http"]).status()["calibration"]
                assert len(status["sets"]) == len(manifest["sets"])
                assert status["selectedSetId"] == manifest["selectedId"]
                assert not status["enabled"], "恢复不得自动启用校正"
                samples.append(time.monotonic() - started)
                if options.switch_all:
                    switches.append(benchmark_switches(ports, status["sets"]))
                    # 下一次启动仍验证原选择；此保存只发生在临时池。
                    manifest["selectedId"] = HttpClient(ports["http"]).status()[
                        "calibration"
                    ]["selectedSetId"]
    print(
        json.dumps(
            {
                "sets": len(manifest["sets"]),
                "manifestSha256": digest,
                "startupSeconds": samples,
                "switches": switches,
                "note": "含首次HTTP状态验证；隔离INFO日志；未清空OS文件缓存，不代表冷盘上限",
            },
            ensure_ascii=False,
        ),
        flush=True,
    )
    if options.max_seconds is not None and max(samples) > options.max_seconds:
        raise RuntimeError(f"启动超出本机基准门槛 {options.max_seconds}s：{samples}")


if __name__ == "__main__":
    main()
