"""使用真实 Node/npm 验证前端增量构建检查，不下载依赖、不改正式前端。"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def write_inputs(path, web, cache, node, npm, sources):
    values = {
        "WEBUI_DIRECTORY": web,
        "WEBUI_CACHE": cache,
        "NODE_EXECUTABLE": node,
        "NPM_EXECUTABLE": npm,
        "WEBUI_SOURCES": ";".join(str(source) for source in sources),
    }
    path.write_text("".join(f"set({key} [==[{value}]==])\n" for key, value in values.items()),
                    encoding="utf-8")


def main():
    require(len(sys.argv) == 5, "usage: WebUiBuildTest.py <cmake> <source-root> <node> <npm>")
    cmake, source, node, npm = sys.argv[1:]
    script = Path(source) / "cmake/build_webui.cmake"
    with tempfile.TemporaryDirectory(prefix="vnapro-webui-build-") as temporary:
        root = Path(temporary) / "前端 构建 空格"
        web, cache, inputs = root / "WebUI", root / "cache/build.cmake", root / "inputs.cmake"
        web.mkdir(parents=True)
        # fixture 只测试构建调度；正式 Vue/Vite 类型检查和产物由真实服务构建验收。
        for package in ("vite", "vue-tsc", "typescript", "vue"):
            directory = web / "node_modules" / package
            directory.mkdir(parents=True)
            (directory / "package.json").write_text("{}", encoding="utf-8")
        package_file = web / "package.json"
        package_file.write_text(json.dumps({"scripts": {"build": "node build.mjs"}}), encoding="utf-8")
        entry = web / "main.txt"
        entry.write_text("first", encoding="utf-8")
        builder = web / "build.mjs"
        builder.write_text("""
import fs from 'node:fs';
if (fs.existsSync('fail-build')) process.exit(7);
const count = fs.existsSync('build-count') ? Number(fs.readFileSync('build-count')) : 0;
fs.writeFileSync('build-count', String(count + 1));
fs.mkdirSync('dist', { recursive: true });
fs.writeFileSync('dist/index.html', fs.readFileSync('main.txt'));
fs.writeFileSync('dist/asset.txt', 'asset');
""", encoding="utf-8")
        write_inputs(inputs, web, cache, node, npm, [package_file, entry, builder, script])

        def run(expected_count, succeeds=True):
            result = subprocess.run([cmake, f"-DINPUT={inputs}", "-P", str(script)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            output = result.stdout.decode("utf-8", errors="replace")
            require((result.returncode == 0) == succeeds,
                    f"前端构建结果不符合预期：exit={result.returncode}\n{output}")
            require(int((web / "build-count").read_text()) == expected_count,
                    f"前端构建次数不符合预期：{output}")
            return output

        run(1)
        require(cache.is_file(), "成功构建未保存增量检查缓存")
        require("up to date" in run(1), "无变更重复构建应跳过 npm")
        entry.write_text("changed", encoding="utf-8")
        run(2)
        require((web / "dist/index.html").read_text() == "changed", "源码更新未进入产物")
        (web / "dist/asset.txt").unlink()
        run(3)
        require((web / "dist/asset.txt").is_file(), "产物删除后未恢复")
        (web / "dist/index.html").write_text("damaged", encoding="utf-8")
        run(4)
        require((web / "dist/index.html").read_text() == "changed", "损坏产物未重新生成")
        previous_output = (web / "dist/index.html").read_bytes()
        entry.write_text("should fail", encoding="utf-8")
        (web / "fail-build").touch()
        require("production build failed" in run(4, succeeds=False), "失败诊断应说明前端构建失败")
        require(not cache.exists(), "失败构建不能留下成功缓存")
        require((web / "dist/index.html").read_bytes() == previous_output, "失败 fixture 不应更新原产物")
        (web / "fail-build").unlink()
        run(5)
        require("up to date" in run(5), "失败恢复后应重新支持增量构建")
    print("WebUI 首次构建、增量跳过、源码更新、产物恢复和失败重试验收通过")


if __name__ == "__main__":
    main()
