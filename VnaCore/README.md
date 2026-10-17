# VnaCore

C++14 静态核心库。公开头位于 `include/vna_core`，AcquisitionCore 直接管理工作线程、命令 FIFO 和发布状态；DeviceSession 是显式包含的设备注入接口。产品入口通过 InstrumentService 使用核心。

按职责组织公开类型：SweepPlan、PortFrame、MeasurementFrame 保留独立头文件；core_types.h 集中声明核心控制接口使用的 CoreState、RunMode、CommandResult/Ticket 和 CoreView。core_command.h 将内部命令与 CoreCommandKind 放在一起。设备接口在 include/vna_core/device_session.h；准备失败异常和原生函数表位于 src/device/plan_preparation_error.h 与 src/device/native/native_functions.h。tests/support 的受控设备、Fixture、并发断言和压力场景各自独立，通过公开的所有权注入构造函数接入。长实现放在对应 cpp，由 vna_core_test_support 仅为测试提供。格式化流程见[项目 README](../README.md)。

普通控制方法返回 `shared_future<CommandResult>`。排队不代表操作生效；检查 ticket 的 `ok` 和运行/配置编号。`TriggerOnce(runId, revision)` 成功时，多源完整帧已发布。`Stop(runId)` 是扫描边界屏障，拒绝零编号和旧运行编号；不关闭 RF。`Close()` 由生命周期拥有线程调用，关闭准入、执行所有已接受命令、尝试关闭 RF，然后 join；不要并发调用 close/析构。

`ReadView()` 一次取得状态、身份和只读帧。Ready 但 revision 为 0 表示尚未配置；Faulted 表示当前硬件配置无效，旧帧仍可供诊断。配置硬件下发失败进入 Faulted；尚未访问硬件的缓冲准备失败保留旧配置。Recover 在没有未完成扫描时重新初始化并应用最后成功配置，产生新 revision，不自动恢复运行。VN10 的 Start 已尝试而完整取数未成功时，扫描状态不明，拒绝重新初始化；RF OFF 不作为中止扫描的依据。

VN08 的身份来自 `DrvGetIDN`；VN10 接口没有身份查询，适配器返回 `VnaPro,TDC1VN10,4PORT,1.0`，不推断厂商或是否为模拟器。服务根据明确的驱动类型区分硬件与模拟后端。

runId、configRevision 和 frameSeq 使用 `uint64_t`，最大值为 `18446744073709551615`。frameSeq 只在同一运行内递增；Start 成功取得新的 runId 后归零，下一完整帧从 1 开始。以每秒一百万帧估算，单次运行也需要约 58 万年才耗尽；HTTP 和 WebUI 将这些编号作为十进制字符串传递，避免 JavaScript 大整数精度丢失。

所有身份计数在递增前检查上限，不循环归零。frameSeq 耗尽时在下一次端口采集前进入 Faulted，保留最后的完整帧；可 Recover 后启动新运行。runId 耗尽时拒绝 Start；configRevision 耗尽时拒绝 Configure，以及需要重新应用成功计划的 Recover。这些拒绝不调用设备，也不修改已有身份、配置或帧。运行或配置编号耗尽后需要重新创建核心实例。

SweepPlan 自有频点、可选不透明 9N 旧接口表和可选逐点 `powerDbm: vector<double>`；非空数组分别必须满足 9N 和 N 长度。功率必须有限且位于 `[-120,100] dBm`，保留负数及小数。VN08 适配器在任何硬件调用前要求完整旧接口表，VN10 不要求它；powerDbm 为空时 VN10 下发 N 个 0 dBm。frequencyHz 可以为空，表示尚未建立物理单位映射。核心不把 native A/B 自动称为 S 参数。sourcePorts 使用 1..4；VN10 的 DrvSetWorkPorts 按同样编号及顺序原样下发，VN08 旧帧使用自己的 sourcePort0 编码。每个 PortFrame 的 IQ 为逐点 16 个 double：接收端口 1..4 各占 `A.real,A.imag,B.real,B.imag` 四项。receiverMask 未激活端口的槽位不能作为有效测量。

设备接口 `AcquireSweep(plan)` 一次返回全部源端口。VN08 适配器内部保留逐源同步 Start/Get；VN10 适配器一次启动工作端口列表扫描，分段等待取数并拆包。核心检查源数量、顺序、端口归属和每源 16N 容量，全部完整后才发布一帧。`AcquisitionCore(path, DriverApi)` 明确选择 ABI；单路径旧构造函数保留 VN08 语义，服务入口默认选择 VN10。两个版本同名函数的签名不同，不自动猜测。

VN10 的每次 64 点 `Get` 成功返回后，已包含这些频点的全部工作源和接收 IQ。核心可通过 `CoreView.sweepProgress` 单独发布真实采集前缀：完整扫频计划保持不变，`completedPoints` 之后的 IQ 统一为 NaN；快照不可变，候选 `frameSeq` 仅用于显示关联，完整帧序号只在所有频点验证成功后推进。Auto 首次显示必须已实际回读至少 1 秒且按已取得点数估算剩余不少于 0.5 秒；已知手动目标至少 1 秒时可提前显示，但预计剩余不足 0.5 秒不首次启用，避免尾段才闪出箭头。启用后最小发布间隔为 100 ms；快扫仍直接发布完整帧。这些项目门槛不是商用仪器的公开参数。新运行、配置、完整帧、故障或关闭都会清除进度。VN08 无法在所有源完成前提供同等前缀，保留完整帧方式。SCPI 数据、标记分析和校准采集仍只使用 `CoreView.frame`。供应头支持阻塞分段取数，但软件探针不证明真实硬件的吞吐和刷新延迟。

SweepPlan 的 `sweepTimeAuto` 默认开启，手动请求 `sweepTimeSeconds` 必须有限且位于 `(0,100000]` 秒。当前 VN10 支持的是 **Software pacing：整轮主机回读的最小节拍**，不宣称修改硬件 ADC 采样、RF 驻留或仪器固有最短扫频。Core 在每段 Get **之前**按单调时钟绝对截止点等待，手动段长按约 100 ms 取 1～64 点；原生 Start 已启动实际扫描，硬件可以先于主机读回完成。Auto 不增加等待并保留原有 64 点取数。`CoreView.manualSweepTimeSupported` 表示设备适配器支持该软件能力，默认设备和 VN08 为 false。`lastSweepTimeSeconds` 仅在真实完整帧提交后报告本轮主机回读实耗，不含任务排队和触发等待；配置或恢复后为 0。短于设备实际耗时的请求不会加速设备。

等待可被同通道、同运行的已接受 Stop、同通道已校验并受理的 Configure 或 Close 唤醒；此后跳过本扫剩余的软件等待，以 64 点段快速取回原生剩余 IQ，直到原有完整帧屏障后才停止、应用新配置或关闭。旧运行和其他通道的 Stop、其他通道的 Configure 及未通过提交校验的配置不提前打断当前通道节拍，不新增跨线程原生调用。如果原生 Get 自身不返回，仍受现有 ABI 无取消入口的限制。`core_sweep_time` 通过独立 ABI 探针检查真实取数截止点、100000 秒目标下的 Stop/Close 安全 drain、多通道独立计时、Auto 无额外等待、能力拒绝及时间边界。

软件资源策略：普通命令队列最多 64 项，Close 有独立保留入口；每帧 IQ 最多 64 MiB，连续帧间至少等待 10 ms，命令可唤醒等待。所有 Drv 调用都在同一工作线程中完成。原生返回码以失败步骤和数值保留，不按模拟器枚举解释厂商码。ABI 没有取消/超时；若原生调用不返回，Stop 和 Close 也无法完成。

原生动态库必须提供绝对路径，不接受 `C:relative` 等路径。Windows 使用 UTF-8 路径转换后的 LoadLibraryExW 与 LOAD_WITH_ALTERED_SEARCH_PATH，从明确的库目录解析依赖；初始化失败返回终结的错误 ticket。共用操作系统句柄管理，VN08/VN10 分别绑定自己的强类型函数表，全部必需符号绑定成功前不调用驱动。VN10 初始化成功后显式关闭 RF；它的 RF OFF 只关端口，不停止扫描，且 ABI 没有设备释放入口。Close 在核心工作线程中等当前整帧读回后关端口并 join 主机 worker，运行期不卸载 VN10 DLL；对象析构后保留加载引用与驱动借用缓冲至进程退出。RF OFF 失败或无法确认扫描结束时，Close 返回失败并保留原始错误，不能宣称设备已停止。

VN10 的四个 SET 数组由设备会话持有，组帧失败时同时保留上次成功配置和本次候选；只有新配置组帧成功后才替换旧列表。取数缓冲在读取失败后仍保持有效。已确认的契约为返回值 0 成功、UINT32_MAX 通用失败、频率 uint64 Hz、IFBW uint32 Hz、功率为 double dBm 且范围 `[-120,100]`、DrvSetWorkPorts 端口编号 1～4。服务显式填写 `instrument.sweep.defaultPowerDbm`；功率和 IFBW 使用各自正确的元素类型，不重解释数组位型。高位返回码同时显示 uint32 原值和有符号 32 位解释，不据此猜测厂商错误含义。

VN10 原生边界诊断位于内部 `device/board/board_diagnostics`，不进入服务或 UI。INFO 记录绑定路径、ABI 元素类型、初始化/配置调用进入和返回、四组实际下发数组的类型/字节数/单位/长度/首末/最小最大值及原生端口列表（unit=one_based）；配置号只用于关联当前会话的下发尝试。DEBUG 在 Download 前按每条至多 16 项记录完整数组。Download 返回失败时 ERROR 也补完整候选值，不要求提前启用 DEBUG；返回码同时保留 unsigned、signed 32 位解释和十六进制。诊断失败不改变设备调用与异常语义，关闭对应等级时不格式化数组。成功的 Start/Get 和逐频点/逐帧路径不增加这些记录。公司现场采集方法见[真实驱动验收](../docs/TDC1VN10真实驱动流程验收.md)。

顶层 CMake 先加入 VnaNativeSimulator 和 VnaBoardSimulator，再加入 VnaCore，可注册两种动态模拟集成测试。核心库只复用 ABI 头，不链接模拟实现。

`core_hardware_board_lifecycle` 使用独立复写九个 VN10 签名的探针，检查同线程调用、SET 数组借用、两/四源分块与分段容量、连续整帧、阻塞 GET 下的关闭准入/收尾、厂商数字错误传播、未完成扫描拒绝恢复，以及运行期 DLL 驻留。它不引用模拟器模型、状态机或错误码；SDK 永久阻塞的取消和实机采集能力不由该探针证明。

`core_sweep_progress` 使用同一独立 ABI 探针，以 130 点、150 ms 分段回读验证亚秒级扫描不闪出预览，再以 256 点、550 ms 分段回读验证超过 1 秒后的真实 128 点前缀、未采 NaN 后缀、完整帧未提前推进和快照不可变，以及失败、新配置、新运行和多通道隔离。该延迟只在测试探针启用；Auto 仍使用 64 点读取。

进度快照的内存分配失败只停用本次扫描余下的预览并清除其显示进度，不中断实际扫描；原生取数错误和完整帧处理错误仍正常上报。`core_progress_allocation_failure` 在已有真实 128 点慢扫前缀、第三段 Get 阻塞后定点注入一次核心显示快照分配失败，验证真实完整 IQ 仍可发布、已保留预览不可变且设备正常停止。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target vna_core_concurrency_tests vna_core_native_tests vna_core_board_tests
cmake -E chdir build ctest -R "core_" --output-on-failure
```

并发测试包含 12 组场景：门控 Stop FIFO 屏障、后置触发拒绝、配置失败与恢复、队列满时 Close 准入，且 64 个已接受触发必须全部成功；采集中成功配置切换、旧帧完整 plan/频轴/IQ 深比较、第二源短 IQ、缺失或重复源归属拒绝，以及三提交者/两读取者的 144 票据压力验证。`core_identity_boundaries` 通过测试专用访问类注入计数上限，调用真实 Configure、Start、TriggerOnce 和 Recover，验证最后一个合法编号、拒绝回绕、完整帧保留及拒绝前没有新增设备调用。VN08 集成测试动态加载旧 DLL，四源、两频点及全部 IQ 分量逐值匹配固定 golden，覆盖频率高 32 位、身份缓存及加载失败。`core_board_integration` 验证 VN10 201 点分段边界、反向源顺序、双/四端口、不可变帧、连续/HOLD、恢复及 ABI 选错；旧表和原始功率可空。全目标 ThreadSanitizer 与端到端数值验收见[验收记录](../docs/模拟设备采集验收.md)。

代码组织：生产类直接声明其职责和状态，不使用 Impl 转发层；CoreCommand 为值存储、promise 随命令移动。多态设备使用 unique_ptr 转移所有权，完成帧使用 shared_ptr<const MeasurementFrame> 支持读取者独立保留。枚举值采用全大写，公有方法采用大驼峰。
