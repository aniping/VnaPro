# VNA 原生接口模拟模块

本目录是独立的原生驱动模拟模块。它只实现当前函数表中的 9 个 `Drv*` C ABI，不依赖 `demo/`、单板或厂商动态库，也不包含 `VnaDriver`、`VnaHal`、校准、DYN 或快扫实现。

## 开发文档

阅读和修改代码请参考[模拟模块开发文档](../docs/模拟模块开发文档.md)，其中包含源码阅读顺序、最小调用示例、帧和 IQ 布局、状态与内存生命周期、错误定位及修改验证入口。范围与验收要求见[硬件模拟方案](../docs/硬件模拟方案.md)。

## 构建与测试

构建需要 CMake 3.13 或更高版本、兼容 C++14 的编译器，以及平台提供的线程库和动态加载库。导出符号测试还需要 `nm`（Linux 通常由 binutils 提供，macOS 由 Xcode Command Line Tools 提供）；Windows MinGW 使用 binutils 的 `objdump` 检查实际 PE 导出表。

在本目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
cmake -E chdir build ctest --output-on-failure
```

生成的模拟库名称为：

- macOS：`libvnb_drv_sim.dylib`
- Linux：`libvnb_drv_sim.so`
- Windows MinGW：`libvnb_drv_sim.dll`

Windows PowerShell 可使用 MinGW 和 Ninja，例如：

```powershell
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=I:/mingw64/bin/g++.exe
cmake --build build-win --parallel
cmake -E chdir build-win ctest --output-on-failure
```

将编译器路径替换为本机路径，并让 MinGW 的 `bin` 目录位于 `PATH` 中，以便运行时找到 `libstdc++`、`libgcc` 和线程库。Windows MinGW 已完成模拟器构建和合同测试；真实厂商 Windows 驱动的调用约定仍须单独验证。

测试分为三部分：

- `native_unit`：直接测试内部状态、逐字段深复制帧快照和固定 IQ 黄金向量（含频率高 32 位）；
- `native_contract`：不链接模拟库，通过 POSIX `dlopen`/`dlsym` 或 Windows `LoadLibraryEx`/`GetProcAddress` 按绝对路径加载，验证原生合同及固定 IQ 黄金向量（含频率高 32 位）；
- `native_export_symbols`：检查动态库只导出允许的 9 个 `Drv*` 符号。

## Sanitizer

Sanitizer 必须使用不同构建目录，不能组合启用：

```bash
cmake -S . -B build-asan -DVNA_SIM_ENABLE_ASAN=ON
cmake -S . -B build-ubsan -DVNA_SIM_ENABLE_UBSAN=ON
cmake -S . -B build-tsan -DVNA_SIM_ENABLE_TSAN=ON
```

每个目录均使用 `cmake --build <目录> --parallel` 和 `cmake -E chdir <目录> ctest --output-on-failure` 完成验证。

## 编译约束

- 代码基线为 C++14；
- macOS 可使用 AppleClang 开发；
- 发布前必须在 GCC 7.3 下完成全量构建和测试。
