# TDC1VN08 原生接口模拟模块

本目录是独立的原生驱动模拟模块。它只实现当前函数表中的 9 个 `Drv*` C ABI，不依赖 `demo/`、单板或厂商动态库，也不包含 `VnaDriver`、`VnaHal`、校准、DYN 或快扫实现。

这是保留的 TDC1VN08 旧接口，库名、签名和黄金向量保持兼容。当前开发以 [TDC1VN10 新接口](../VnaBoardSimulator/README.md)为准；服务默认 VN10，通过 `--board tdc1vn08` 显式运行本模块。两版同名函数签名不同，不能只替换 DLL 路径；VN08 未来使用新接口后，应明确选择新接口适配器。

## 开发文档

阅读和修改代码请参考[模拟模块开发文档](../docs/模拟模块开发文档.md)，其中包含源码阅读顺序、最小调用示例、帧和 IQ 布局、状态与内存生命周期、错误定位及修改验证入口。范围与验收要求见[硬件模拟方案](../docs/硬件模拟方案.md)。

源码遵守 [C++语言编程规范](../docs/C++语言编程规范.md)，文件使用小写下划线命名，类型和接口按职责拆分：

- `include/vna_native/native_api_contract.h`：保留九个 `Drv*` 的名称与签名，集中声明 ABI 和强类型函数指针。
- `src/native_sim/native_simulator_state.h`：直接持有配置、帧槽位和待取结果；一把互斥锁保护完整调用。
- `normal_frame_snapshot.h`：集中定义同一帧职责的借用 ABI 输入、解码点和自有帧数据。
- `native_frame_slot.h`、`native_pending_sweep.h`：分别表示可替换的下载帧和成功 Start 冻结的 IQ。
- `normal_frame_decoder.h`、`signal_model.h`：提供命名空间函数，校验、复制、生成与提交分开实现。
- `failure_injector.h`：仅供单元测试借用故障注入器；测试对象必须覆盖调用期，完成后显式清除借用。

测试使用普通断言函数与显式文件、行号，测试组直接注册；线程由 `JoinedThreads` 持有并在退出前统一汇合。黄金向量和独立公式保留原值，默认黄金模式及噪声模式的序列语义不变。

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

## 可选连续观察噪声

默认未设置 `VNAPRO_SIM_MODE` 或设置为 `golden` 时，重复扫描仍输出原有固定 IQ 黄金向量。需要观察 Web 界面连续更新时，可在启动使用模拟库的进程前设置：

```powershell
$env:VNAPRO_SIM_MODE = 'noise'
$env:VNAPRO_SIM_SEED = '42'
# 随后启动使用本模拟库的服务或采集程序。
```

Linux/macOS 可使用 `VNAPRO_SIM_MODE=noise VNAPRO_SIM_SEED=42 <程序及参数>`。种子仅在 `noise` 模式读取，接受 `0`～`18446744073709551615` 的十进制整数，未设置或空值默认为 `1`。模式仅接受 `golden`、`noise`，空值等同默认；非法模式或噪声种子使 `DrvHwInit` 返回 `INVALID_ARGUMENT`，原配置、帧和 pending 保持不变。

`DrvHwInit` 在状态锁内冻结模式和种子，并清零扫描序号。噪声由种子、成功 `DrvStartSweep` 序号、完整频率、激励端口、接收端口及 A/B 波确定；每个复数波的加性扰动模长最多为黄金波幅的 **0.2%**，保持有限 IQ 和原有 `16N` 布局。它只是测试扰动，不模拟真实射频噪声、S 参数或校准。

同一初始化周期内，所有激励端口共用成功 Start 序号。成功 Start 生成并缓存一次结果，失败 Start 不推进序号；错误 Get 不消费缓存，正确 Get 仍只能成功一次。关闭 RF 丢弃已成功 Start 的 pending，不回退序号。用同一种子重新 Init，再按相同帧及 Start 顺序执行，可重放相同数据。

运行期间更改环境不会改变已初始化实例；切换模式须重新 Init，不能在同一 pending 中混用两种模式。环境是进程配置，应在初始化前设置，不应并发修改。模拟器单元及动态合同测试主动隔离外部环境，分别验证默认黄金向量和噪声模式。

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
