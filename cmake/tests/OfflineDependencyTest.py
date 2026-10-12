"""用隔离目录验证本地依赖解压，不使用已有源码或全局下载缓存。"""

from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile


ARCHIVE_NAME = "release-fixture-1.0"
ARCHIVE_ROOT = "fixture-1.0"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def create_archive(path, root=ARCHIVE_ROOT, content="# 本地发布包测试源码\n".encode("utf-8")):
    with tarfile.open(path, "w:gz") as archive:
        item = tarfile.TarInfo(root + "/CMakeLists.txt")
        item.size = len(content)
        archive.addfile(item, io.BytesIO(content))
    return sha256(path.read_bytes()).hexdigest()


def write_project(source, module, digest):
    # 路径来自工具和隔离临时目录；统一使用 CMake 支持的正斜线。
    require('"' not in module.as_posix(), "测试工具路径不能包含引号")
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.13)\n"
        "project(OfflineDependencyTest LANGUAGES NONE)\n"
        f'include("{module.as_posix()}")\n'
        f'vnapro_extract_local_archive("{ARCHIVE_NAME}" "{ARCHIVE_ROOT}" "{digest}" actual_source)\n'
        'file(WRITE "${CMAKE_BINARY_DIR}/source.txt" "${actual_source}")\n',
        encoding="utf-8",
    )


def configure(cmake, source, build, expected_error=None):
    environment = dict(os.environ)
    for key in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
        environment[key] = "http://127.0.0.1:9"
    environment["NO_PROXY"] = ""
    environment["no_proxy"] = ""
    result = subprocess.run(
        [cmake, "-S", str(source), "-B", str(build)],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
        check=False,
    )
    if expected_error:
        require(result.returncode != 0 and expected_error in result.stdout, result.stdout)
        return None
    require(result.returncode == 0, result.stdout)
    return Path((build / "source.txt").read_text(encoding="utf-8"))


def exercise(cmake, source, module):
    third_part = source / "third-part"
    third_part.mkdir()
    archive = third_part / (ARCHIVE_NAME + ".tar.gz")
    write_project(source, module, "0" * 64)
    configure(cmake, source, source / "missing", "Missing third-party archive")
    digest = create_archive(archive)
    configure(cmake, source, source / "wrong-hash", "SHA256 mismatch")
    require(not (third_part / ARCHIVE_NAME).exists(), "校验失败前不能创建解压源码")
    archive.write_bytes(b"invalid archive")
    write_project(source, module, sha256(archive.read_bytes()).hexdigest())
    configure(cmake, source, source / "invalid-archive", "Cannot extract third-party archive")
    digest = create_archive(archive, root="unexpected-root")
    write_project(source, module, digest)
    configure(cmake, source, source / "wrong-root", "Expected source root")
    require(not list(third_part.glob(".extract-*")), "失败解压不能遗留半成品目录")
    digest = create_archive(archive)
    write_project(source, module, digest)
    # 两个全新构建目录并发首次配置，共享同一套完整发布的依赖源码。
    with ThreadPoolExecutor(max_workers=2) as executor:
        futures = [executor.submit(configure, cmake, source, source / name) for name in ("first", "second")]
        paths = [future.result() for future in futures]
    require(paths[0] == paths[1], "并发配置必须引用同一份已发布源码")
    actual = paths[0]
    require(actual == third_part / ARCHIVE_NAME, "正式源码必须直接平铺在 third-part 下")
    require((actual / "CMakeLists.txt").is_file(), "成功配置必须发布完整源码根")
    stamp = actual / ".vnapro-archive-sha256"
    require(stamp.read_text().strip() == digest, "解压标记必须匹配压缩包身份")
    modified_time = (actual / "CMakeLists.txt").stat().st_mtime_ns
    require(configure(cmake, source, source / "first") == actual, "再次配置应复用相同源码")
    require((actual / "CMakeLists.txt").stat().st_mtime_ns == modified_time, "复用不能重写源码")
    compressed = archive.read_bytes()
    archive.unlink()
    configure(cmake, source, source / "missing-after-extract", "Missing third-party archive")
    archive.write_bytes(compressed)
    stamp.write_text("invalid checksum marker\n", encoding="utf-8")
    configure(cmake, source, source / "invalid-marker", "source checksum marker mismatch")


def main():
    cmake, repository = sys.argv[1:3]
    module = Path(repository).resolve() / "cmake" / "local_archive.cmake"
    with tempfile.TemporaryDirectory(prefix="vnapro-offline-dependencies-") as directory:
        source = Path(directory).resolve()
        exercise(cmake, source, module)
    print("离线依赖验收通过：缺包、错摘要、坏包、源码根、并发解压、复用及标记校验")


if __name__ == "__main__":
    main()
