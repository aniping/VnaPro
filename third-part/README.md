# C++ 第三方库

本目录保存固定版本的官方 release 源码压缩包。Git 和 GitHub 仓库只保存原始压缩包与本说明；解压源码和编译产物不提交，也不使用 Git LFS。CMake 构建期间不访问下载服务，缺包或校验失败直接终止。

```text
third-part/
├── README.md
├── spdlog-1.17.0.tar.gz       # 纳入 Git
├── yaml-cpp-0.9.0.tar.gz     # 纳入 Git
├── spdlog-1.17.0/           # 自动解压的源码，不纳入 Git
│   ├── CMakeLists.txt
│   └── .vnapro-archive-sha256
└── yaml-cpp-0.9.0/          # 自动解压的源码，不纳入 Git
    ├── CMakeLists.txt
    └── .vnapro-archive-sha256
```

## 固定版本与来源

| 库 | 版本 | 本地源码包 | 字节数 | SHA256 |
| --- | --- | --- | --- | --- |
| spdlog | 1.17.0 | `spdlog-1.17.0.tar.gz` | 281801 | `d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744` |
| yaml-cpp | 0.9.0 | `yaml-cpp-0.9.0.tar.gz` | 1056093 | `25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a` |

- spdlog：[官方 release](https://github.com/gabime/spdlog/releases/tag/v1.17.0)，[对应标签源码包](https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.17.0)。压缩包内根目录为 `spdlog-1.17.0`。
- yaml-cpp：[官方 release](https://github.com/jbeder/yaml-cpp/releases/tag/yaml-cpp-0.9.0)，[对应标签源码包](https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/yaml-cpp-0.9.0)。压缩包内根目录为 `yaml-cpp-yaml-cpp-0.9.0`。

摘要由本项目对下载的官方源码包计算，用于核对仓库内原始包，不表示上游另行签署了该摘要。两个库均使用 MIT 许可；spdlog 随附的 fmt 及其许可也完整保留在包内。使用与分发时保留原始版权和许可文件。

## 本地构建

具备 CMake、编译器和构建工具后，执行[根目录构建步骤](../README.md#构建和测试)即可。无需手工解压，也无需指定外部依赖源码目录；旧 `FETCHCONTENT_SOURCE_DIR_*` 参数不再生效。此目录只包含 C++ 库，WebUI 首次 `npm ci` 仍需访问 npm 仓库。

`cmake/local_archive.cmake` 每次配置校验压缩包 SHA256，首次使用对应版本时通过 CMake 自带的 tar 命令解压。解压后的库目录直接平铺在 `third-part` 下，名称与源码包名一致；yaml-cpp 的上游目录 `yaml-cpp-yaml-cpp-0.9.0` 在发布源码时重命名为 `yaml-cpp-0.9.0`。完整解压后在库的源码根目录写入 `.vnapro-archive-sha256` 标记，再供 `add_subdirectory` 引用；同一版本可供多个构建目录复用。解压锁文件和 `.extract-*` 暂存目录位于 `third-part` 根目录，均不进入 Git。不同工具链的编译产物分别放在各自构建目录。

缺少或损坏的包应从仓库恢复原始文件，不会自动联网修复。解压目录缺失 CMakeLists.txt 或摘要标记不符时，按报错定位并移除该生成目录，再执行 CMake 配置重新解压。不要在生成目录内维护第三方库修改。

## 升级依赖

1. 在可联网环境核对上游官方 release，下载指定标签的原始源码压缩包，保持包内容不变。
2. 将包保存到本目录，记录版本、上游解压根目录、文件字节数和 SHA256；Windows 可用 `Get-FileHash -Algorithm SHA256` 计算摘要。
3. 同步更新 `cmake/spdlog.cmake` 或 `cmake/yaml_cpp.cmake` 的包名、根目录和摘要，以及本说明的清单。保持既有静态库、C++14 和关闭上游测试/示例的配置。
4. 从本地包重新配置、构建并运行相关测试，核对 Windows/Linux 工具链、许可与业务行为。升级后的离线验收结果按实际执行情况记录。
5. 按功能提交源码压缩包和配置、文档；移除已不再引用的旧包。Git 不接收解压源码、锁文件、暂存目录或第三方编译产物。

## 离线验收

2026-10-12 验证最终平铺布局：Windows MinGW/GCC 10.3 全量 **25/25** CTest 通过（32.58 秒），WSL/Linux GCC 13.3 全量 **25/25** 通过（17.29 秒）。两套隔离源码副本首次配置时只携带仓内压缩包，没有当前版本的解压目录；构建使用不可用代理，并故意指定不存在的旧 FetchContent 源码覆盖。实际编译路径均为平铺的 `third-part/spdlog-1.17.0` 和 `third-part/yaml-cpp-0.9.0`，旧覆盖参数未参与配置。CLion 的 `cmake-build-debug/bin/vna_server.exe` 也已按本地源码重新构建。

新增 `offline_dependencies` 测试在独立临时目录生成测试压缩包，验证缺包、摘要错误、损坏包、错误源码根、并发首次解压、源码复用、已解压后缺包以及摘要标记错误。运行方式：

```powershell
cmake -E chdir build ctest -R offline_dependencies --output-on-failure
```

这些证据确认构建没有依赖下载入口和历史源码缓存；不可用代理不是操作系统断网设置。本次只处理 C++ 依赖，前端沿用现有 npm 安装方式。
