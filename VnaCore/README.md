# VnaCore

C++14 静态核心库。公开头位于 `include/vna_core`，设备、线程和测试注入接口均为私有实现。产品入口通过 InstrumentService 使用核心。

公开类型分文件：SweepPlan、PortFrame、MeasurementFrame、CommandResult、CoreView；CoreState.h 声明状态/运行枚举，CoreTypes.h 仅作为兼容聚合入口。设备接口、准备失败异常和原生函数表分别位于 DeviceSession.h、PlanPreparationError.h、native/NativeFunctions.h。tests/support 的测试注入入口、受控设备、Fixture 和 RAII 助手各自独立，不暴露给生产调用方。格式化流程见[项目 README](../README.md)。

普通控制方法返回 `shared_future<CommandResult>`。排队不代表操作生效；检查 ticket 的 `ok` 和运行/配置编号。`triggerOnce(runId, revision)` 成功时，多源完整帧已发布。`stop(runId)` 是扫描边界屏障，拒绝零编号和旧运行编号；不关闭 RF。`close()` 由生命周期拥有线程调用，关闭准入、执行所有已接受命令、尝试关闭 RF，然后 join；不要并发调用 close/析构。

`readView()` 一次取得状态、身份和只读帧。Ready 但 revision 为 0 表示尚未配置；Faulted 表示当前硬件配置无效，旧帧仍可供诊断。配置硬件下发失败进入 Faulted；尚未访问硬件的缓冲准备失败保留旧配置。Recover 重新初始化并应用最后成功配置，产生新 revision，不自动恢复运行。

SweepPlan 自有频点和不透明 9N 表；frequencyHz 可以为空，表示尚未建立物理单位映射。核心不解释功率表，也不把 native A/B 自动称为 S 参数。sourcePorts 使用 1..4，设备适配转换为 native 0..3。每个 PortFrame 的 IQ 为逐点 16 个 double：接收端口 1..4 各占 `A.real,A.imag,B.real,B.imag` 四项。receiverMask 未激活端口的槽位不能作为有效测量。

软件资源策略：普通命令队列最多 64 项，Close 有独立保留入口；每帧 IQ 最多 64 MiB，连续帧间至少等待 10 ms，命令可唤醒等待。所有 Drv 调用都在同一工作线程中完成。原生返回码以失败步骤和数值保留，不按模拟器枚举解释厂商码。ABI 没有取消/超时；若原生调用不返回，Stop 和 Close 也无法完成。

原生动态库必须提供绝对路径，不接受 `C:relative` 等路径。Windows 使用 LoadLibraryExA 与 LOAD_WITH_ALTERED_SEARCH_PATH，从明确的库目录解析依赖；初始化失败返回终结的错误 ticket。

顶层 CMake 先加入 VnaNativeSimulator，再加入 VnaCore，可注册动态模拟集成测试。核心库只复用 ABI 头，不链接模拟实现。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target vna_core_concurrency_tests vna_core_native_tests
cmake -E chdir build ctest -R "core_" --output-on-failure
```

并发测试包含 11 组场景：门控 Stop FIFO 屏障、后置触发拒绝、配置失败与恢复、队列满时 Close 准入，且 64 个已接受触发必须全部成功；新增采集中成功配置切换、旧帧完整 plan/频轴/IQ 深比较、第二源短 IQ，以及三提交者/两读取者的 144 票据压力验证。集成测试动态加载模拟 DLL，四源、两频点及全部 IQ 分量逐值匹配固定 golden，并覆盖频率高 32 位、身份缓存及加载失败。全目标 ThreadSanitizer 与端到端数值验收见[验收记录](../docs/模拟设备采集验收.md)。
