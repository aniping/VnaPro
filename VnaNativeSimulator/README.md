# VNA 原生接口模拟模块

本目录是独立的原生驱动模拟模块。它只实现当前函数表中的 9 个 `Drv*` C ABI，不依赖 `demo/`、单板或厂商动态库，也不包含 `VnaDriver`、`VnaHal`、校准、DYN 或快扫实现。

## 构建与测试

构建需要 CMake 3.13 或更高版本、兼容 C++14 的编译器，以及平台提供的线程库和动态加载库。导出符号测试还需要 `nm`（Linux 通常由 binutils 提供，macOS 由 Xcode Command Line Tools 提供）。

在本目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

生成的模拟库名称为：

- macOS：`libvnb_drv_sim.dylib`
- Linux：`libvnb_drv_sim.so`

测试分为三部分：

- `native_unit`：直接测试内部状态、逐字段深复制帧快照和固定 IQ 黄金向量（含频率高 32 位）；
- `native_contract`：不链接模拟库，只通过 `dlopen`/`dlsym` 验证原生合同及固定 IQ 黄金向量（含频率高 32 位）；
- `native_export_symbols`：检查动态库只导出允许的 9 个 `Drv*` 符号。

## Sanitizer

Sanitizer 必须使用不同构建目录，不能组合启用：

```bash
cmake -S . -B build-asan -DVNA_SIM_ENABLE_ASAN=ON
cmake -S . -B build-ubsan -DVNA_SIM_ENABLE_UBSAN=ON
cmake -S . -B build-tsan -DVNA_SIM_ENABLE_TSAN=ON
```

每个目录均使用 `cmake --build <目录> --parallel` 和 `ctest --test-dir <目录> --output-on-failure` 完成验证。

## 编译约束

- 代码基线为 C++14；
- macOS 可使用 AppleClang 开发；
- 发布前必须在 GCC 7.3 下完成全量构建和测试。
