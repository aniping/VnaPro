# VnaCore

C++14 静态核心库。公开头位于 `include/vna_core`，AcquisitionCore 直接管理工作线程、命令 FIFO 和发布状态；DeviceSession 是显式包含的设备注入接口。产品入口通过 InstrumentService 使用核心。

按职责组织公开类型：SweepPlan、PortFrame、MeasurementFrame 保留独立头文件；core_types.h 集中声明核心控制接口使用的 CoreState、RunMode、CommandResult/Ticket 和 CoreView。core_command.h 将内部命令与 CoreCommandKind 放在一起。设备接口在 include/vna_core/device_session.h；准备失败异常和原生函数表位于 src/device/plan_preparation_error.h 与 src/device/native/native_functions.h。tests/support 的受控设备、Fixture、并发断言和压力场景各自独立，通过公开的所有权注入构造函数接入。长实现放在对应 cpp，由 vna_core_test_support 仅为测试提供。格式化流程见[项目 README](../README.md)。

普通控制方法返回 `shared_future<CommandResult>`。排队不代表操作生效；检查 ticket 的 `ok` 和运行/配置编号。`TriggerOnce(runId, revision)` 成功时，多源完整帧已发布。`Stop(runId)` 是扫描边界屏障，拒绝零编号和旧运行编号；不关闭 RF。`Close()` 由生命周期拥有线程调用，关闭准入、执行所有已接受命令、尝试关闭 RF，然后 join；不要并发调用 close/析构。

`ReadView()` 一次取得状态、身份和只读帧。Ready 但 revision 为 0 表示尚未配置；Faulted 表示当前硬件配置无效，旧帧仍可供诊断。配置硬件下发失败进入 Faulted；尚未访问硬件的缓冲准备失败保留旧配置。Recover 在没有未完成扫描时重新初始化并应用最后成功配置，产生新 revision，不自动恢复运行。VN10 的 Start 已尝试而完整取数未成功时，扫描状态不明，拒绝重新初始化；RF OFF 不作为中止扫描的依据。

VN08 的身份来自 `DrvGetIDN`；VN10 接口没有身份查询，适配器返回 `VnaPro,TDC1VN10,4PORT,1.0`，不推断厂商或是否为模拟器。服务根据明确的驱动类型区分硬件与模拟后端。

runId、configRevision 和 frameSeq 使用 `uint64_t`，最大值为 `18446744073709551615`。frameSeq 只在同一运行内递增；Start 成功取得新的 runId 后归零，下一完整帧从 1 开始。以每秒一百万帧估算，单次运行也需要约 58 万年才耗尽；HTTP 和 WebUI 将这些编号作为十进制字符串传递，避免 JavaScript 大整数精度丢失。

所有身份计数在递增前检查上限，不循环归零。frameSeq 耗尽时在下一次端口采集前进入 Faulted，保留最后的完整帧；可 Recover 后启动新运行。runId 耗尽时拒绝 Start；configRevision 耗尽时拒绝 Configure，以及需要重新应用成功计划的 Recover。这些拒绝不调用设备，也不修改已有身份、配置或帧。运行或配置编号耗尽后需要重新创建核心实例。

SweepPlan 自有频点、可选不透明 9N 旧接口表和可选逐点 `powerRaw`；非空数组分别必须满足 9N 和 N 长度。VN08 适配器在任何硬件调用前要求完整旧接口表，VN10 不要求它；powerRaw 为空时 VN10 下发 N 个零。frequencyHz 可以为空，表示尚未建立物理单位映射。核心不解释功率编码，也不把 native A/B 自动称为 S 参数。sourcePorts 使用 1..4，设备适配转换为 native 0..3。每个 PortFrame 的 IQ 为逐点 16 个 double：接收端口 1..4 各占 `A.real,A.imag,B.real,B.imag` 四项。receiverMask 未激活端口的槽位不能作为有效测量。

设备接口 `AcquireSweep(plan)` 一次返回全部源端口。VN08 适配器内部保留逐源同步 Start/Get；VN10 适配器一次启动工作端口列表扫描，分段等待取数并拆包。核心检查源数量、顺序、端口归属和每源 16N 容量，全部完整后才发布一帧。`AcquisitionCore(path, DriverApi)` 明确选择 ABI；单路径旧构造函数保留 VN08 语义，服务入口默认选择 VN10。两个版本同名函数的签名不同，不自动猜测。

软件资源策略：普通命令队列最多 64 项，Close 有独立保留入口；每帧 IQ 最多 64 MiB，连续帧间至少等待 10 ms，命令可唤醒等待。所有 Drv 调用都在同一工作线程中完成。原生返回码以失败步骤和数值保留，不按模拟器枚举解释厂商码。ABI 没有取消/超时；若原生调用不返回，Stop 和 Close 也无法完成。

原生动态库必须提供绝对路径，不接受 `C:relative` 等路径。Windows 使用 UTF-8 路径转换后的 LoadLibraryExW 与 LOAD_WITH_ALTERED_SEARCH_PATH，从明确的库目录解析依赖；初始化失败返回终结的错误 ticket。共用操作系统句柄管理，VN08/VN10 分别绑定自己的强类型函数表，全部必需符号绑定成功前不调用驱动。VN10 初始化成功后显式关闭 RF；它的 RF OFF 只关端口，不停止扫描，且 ABI 没有设备释放入口。Close 在核心工作线程中等当前整帧读回后关端口并 join 主机 worker，运行期不卸载 VN10 DLL；对象析构后保留加载引用与驱动借用缓冲至进程退出。RF OFF 失败或无法确认扫描结束时，Close 返回失败并保留原始错误，不能宣称设备已停止。

VN10 的四个 SET 数组由设备会话持有，组帧失败时同时保留上次成功配置和本次候选；只有新配置组帧成功后才替换旧列表。取数缓冲在读取失败后仍保持有效。已确认的原始契约为返回值 0 成功、频率与 IFBW 使用 Hz、原始功率 0 合法、端口编号 0～3；功率原始编码不等于 dBm。服务显式填写 `instrument.sweep.defaultPowerRaw`，不在适配器内替换真实采样值。

顶层 CMake 先加入 VnaNativeSimulator 和 VnaBoardSimulator，再加入 VnaCore，可注册两种动态模拟集成测试。核心库只复用 ABI 头，不链接模拟实现。

`core_hardware_board_lifecycle` 使用独立复写九个 VN10 签名的探针，检查同线程调用、SET 数组借用、两/四源分块与分段容量、连续整帧、阻塞 GET 下的关闭准入/收尾、厂商数字错误传播、未完成扫描拒绝恢复，以及运行期 DLL 驻留。它不引用模拟器模型、状态机或错误码；SDK 永久阻塞的取消和实机采集能力不由该探针证明。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target vna_core_concurrency_tests vna_core_native_tests vna_core_board_tests
cmake -E chdir build ctest -R "core_" --output-on-failure
```

并发测试包含 12 组场景：门控 Stop FIFO 屏障、后置触发拒绝、配置失败与恢复、队列满时 Close 准入，且 64 个已接受触发必须全部成功；采集中成功配置切换、旧帧完整 plan/频轴/IQ 深比较、第二源短 IQ、缺失或重复源归属拒绝，以及三提交者/两读取者的 144 票据压力验证。`core_identity_boundaries` 通过测试专用访问类注入计数上限，调用真实 Configure、Start、TriggerOnce 和 Recover，验证最后一个合法编号、拒绝回绕、完整帧保留及拒绝前没有新增设备调用。VN08 集成测试动态加载旧 DLL，四源、两频点及全部 IQ 分量逐值匹配固定 golden，覆盖频率高 32 位、身份缓存及加载失败。`core_board_integration` 验证 VN10 201 点分段边界、反向源顺序、双/四端口、不可变帧、连续/HOLD、恢复及 ABI 选错；旧表和原始功率可空。全目标 ThreadSanitizer 与端到端数值验收见[验收记录](../docs/模拟设备采集验收.md)。

代码组织：生产类直接声明其职责和状态，不使用 Impl 转发层；CoreCommand 为值存储、promise 随命令移动。多态设备使用 unique_ptr 转移所有权，完成帧使用 shared_ptr<const MeasurementFrame> 支持读取者独立保留。枚举值采用全大写，公有方法采用大驼峰。
