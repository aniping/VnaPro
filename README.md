# VnaPro

实现模拟设备的完整采数链路：Native 动态库 → 单 worker 采集核心 → 仪器服务 → TCP SCPI / Web。支持 Keysight N52xxB PNA/PNA-X 的已核对命令子集，Web 界面参考罗德与施瓦茨 ZNB8 的仪器布局；设备身份如实返回 VnaPro，当前后台只允许本项目模拟器。

## 构建和测试

Web 前端是独立的 Vue 3 + TypeScript + Vite 工程，需要 Node.js ≥ 22.13 和 npm。服务需要 CMake ≥ 3.13、C++14 编译器、线程库；网络集成测试使用 Python 3 标准库。C++ 第三方库使用仓库内 `third-part` 的官方 release 源码压缩包：spdlog 1.17.0 和 yaml-cpp 0.9.0。CMake 校验 SHA256 后自动解压为 `third-part/spdlog-1.17.0` 和 `third-part/yaml-cpp-0.9.0`，直接引用这两个源码目录构建，不下载依赖。包清单和升级方式见[第三方库说明](third-part/README.md)。Windows MinGW 的 `bin` 必须加入 PATH。在项目根目录先构建前端，再构建服务：

```powershell
Set-Location WebUI
npm.cmd ci
npm.cmd test
npm.cmd run build
Set-Location ..
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=I:/mingw64/bin/g++.exe
cmake --build build --parallel
cmake -E chdir build ctest --output-on-failure
```

替换编译器路径。Linux/macOS 可去掉 MinGW 编译器参数，使用当地工具链。独立构建见 [TDC1VN10 模拟驱动](VnaBoardSimulator/README.md)及保留的 [TDC1VN08 模拟驱动](VnaNativeSimulator/README.md)。本轮已验证 Windows MinGW/GCC 10.3 和 WSL/Linux GCC 13.3 的全目标 ThreadSanitizer；macOS 和既定 GCC 7.3 发布门禁需在对应环境复验。

`npm run build` 包含 Vue/TypeScript 类型检查，产物在 `WebUI/dist`。本次仅打包 C++ 库，WebUI 的首次 `npm ci` 仍需访问 npm 仓库；前端依赖不包含在这些压缩包内。公司离线环境具备 C++ 工具链后，可直接执行上述 CMake 命令，无需依赖源码路径覆盖参数。只构建或运行 SCPI 服务可跳过 Node/npm 步骤，并以 `--no-web` 启动；Web HTTP 集成测试需要先生成前端产物。旧 `FETCHCONTENT_SOURCE_DIR_*` 参数不再生效。前端开发与目录说明见 [WebUI/README.md](WebUI/README.md)。

## C++ 文件组织与格式

C++ 开发统一遵守 [C++语言编程规范](docs/C++语言编程规范.md)。Git 仍按功能拆分提交，提交说明使用中文，并遵守已约定的提交行数限制。

代码说明注释统一使用中文，标识符、接口名和协议关键字保留原拼写。版权声明维持既定格式；clang-format 等工具识别的指令保留原形式。格式器关闭 `FixNamespaceComments`，保留中文的命名空间收尾注释。

独立行为类分别使用同名文件；简单结构体和枚举按职责放在所属类或同一数据类型头中，头文件声明接口、源文件实现行为。AcquisitionCore、InstrumentService、ScpiEngine、HttpApi 和 WebEndpoint 直接声明状态，不再通过 PImpl 或转发类隐藏。`core_types.h` 定义 CoreState、RunMode、CommandResult/Ticket 和 CoreView；`core_command.h` 放内部命令及其枚举；`instrument_settings.h` 放 InstrumentSettings、ConfigPatch、TriggerSource 和 SweepMode；`service_task.h` 放服务任务及 Kind/Stage。SCPI 消息和 OPC 记录归 ScpiEngine，HTTP 会话和操作记录归 HttpApi，HTTP 连接状态归 WebEndpoint；共享请求、响应和 Web 配置放在 `http_types.h`。JSON 值、解析器和编码器各自独立，SCPI/Web 共用 `src/network` 的平台适配与 Socket 资源管理。测试助手位于各模块的 tests。文件名使用 snake_case，C++ 函数和命名空间使用大驼峰，类成员以 `_` 结尾；SCPI、JSON 和九个 Drv* ABI 的外部名称保持其合同。

先正常编写代码，再直接运行 clang-format 程序，使用根目录 `.clang-format` 自动格式化，排版依据上述规范第 3 章。单条语句的条件、循环也自动补上 `{}`（InsertBraces: true）；短函数体展开，类、结构体和函数定义之间自动留一空行（SeparateDefinitionBlocks: Always）；120 列由格式器处理，不为满足提交行数压缩代码。按功能拆分提交，不通过省略换行缩小差异。使用 clang-format 18.1.8，在根目录运行：

```powershell
$cppFiles = rg --files VnaCore VnaMeasurement VnaService VnaServer VnaNativeSimulator VnaBoardSimulator VnaLog -g '*.h' -g '*.cpp'
clang-format --style=file -i $cppFiles
clang-format --style=file --dry-run --Werror $cppFiles
```

日常修改只需将本次变更的文件传给格式器。其他 C++ 模块同样使用根目录配置。配置各项有中文注释：按 C++14 解析、固定四空格缩进和指针位置，展开短 Lambda/枚举，保留一行定义间空白及 C++ 注释的人工分行。Standard 只影响格式解析，不替代 CMake 的编译设置。选项含义参照 [clang-format 18.1.8 官方文档](https://releases.llvm.org/18.1.8/tools/clang/docs/ClangFormatStyleOptions.html)。

对象状态优先使用值成员。多态设备会话仍通过 `unique_ptr` 转交所有权，已发布的不可变帧通过 `shared_ptr<const ...>` 供读者保留；这些生命周期需求继续采用 RAII。九个 Drv* 函数的 C ABI、原生数组和平台句柄保持边界约定。本项目保留 C++14，原始 demo 参考代码不参与此次整改。

## 启动与测量

```powershell
.\build\bin\vna_server.exe --bind 127.0.0.1 --port 5025
```

默认加载构建生成的 **TDC1VN10 新接口模拟库**，启动 SCPI `127.0.0.1:5025` 和 Web `http://127.0.0.1:8080`。`--board tdc1vn08` 使用保留的 VN08 旧接口库 `tdc1vn08_drv_sim`，`--board tdc1vn10` 显式选择当前基准接口 `tdc1vn10_drv_sim`；CLion、命令行和部署路径的具体设置见[模拟驱动切换](docs/模拟驱动切换.md)。未来 VN08 驱动对齐新接口后应使用新接口适配器。两个接口有同名异签名函数，不能仅换 DLL 路径。静态根目录为构建时指定的 `WebUI/dist`，浏览器打开 [Web 面板](http://127.0.0.1:8080/) 即可操作。迁移产物时传入与 `--board` 匹配的 `--driver` 绝对路径，并用 `--web-root` 指向部署后的 `WebUI/dist`；不能把未构建的 `WebUI/src` 作为静态根目录。`--port 0` / `--web-port 0` 自动选择空闲端口，实际地址由 INFO 监听日志报告；关闭控制台输出时可在日志文件读取，日志级别须允许 INFO。SCPI 与 Web 分别通过 `--bind` / `--web-bind` 指定 IPv4 地址，默认绑定本机；`--no-web` 关闭 Web 入口。

## 仪器与日志配置

统一使用 UTF-8 YAML，仪器配置在 `instrument` 下，所有日志配置在 `log` 下。默认 **4 端口、支持 10 MHz–8 GHz，启动扫描覆盖相同频段，201 点、IFBW 1 kHz**。新服务默认单迹线 **Trc1/S21、dB Mag、HOLD**，单端口配置回退为 S11；没有初始假帧。浏览器重新连接读取已有测量，不强制重置设备。`instrument.portCount` 可配置为 1–4，正式验收覆盖 2 端口和 4 端口。配置值是本项目模拟器的运行能力，增加端口数配置不会自动扩展为完整 4×4 S 参数矩阵；本版 S 参数为 S11/S21/S12/S22，RAW 可以使用已配置的全部端口。

```yaml
instrument:
  portCount: 4
  frequency:
    minHz: 10000000
    maxHz: 8000000000
  sweep:
    defaultStartHz: 10000000
    defaultStopHz: 8000000000
    defaultPoints: 201
    defaultIfBandwidthHz: 1000
```

频率配置使用不加引号的正十进制整数 Hz，点数支持 2–10001。默认频率必须落在支持范围内，IFBW 默认值必须为支持档位。配置在启动时验证并冻结，HTTP/SCPI 的设置和测量源/接收端口都受此能力约束；Web 输入范围从 `/api/v1/status` 的 `capabilities` 读取。错误配置在创建设备前退出，退出码为 2。省略配置项使用程序默认值；修改支持范围时应同时调整默认扫描范围。

服务采用 spdlog，默认 INFO，同时输出终端 stderr 和构建目录 `bin/logs/vnapro.log`；日志包含毫秒时间、等级、线程、模块和消息。单文件 10 MiB，保留 5 个备份，单条完整记录最多 64 KiB；逐帧摘要只在 TRACE 打开时输出。

默认配置为 [config/vnapro.yml](config/vnapro.yml)，CMake 将它复制到构建目录 `bin/vnapro.yml`。可用 `--config 配置文件` 指定部署配置，相对日志路径按配置文件目录解析；修改配置后重启生效。大小、备份、刷新、格式、中文路径及历史文件边界见[日志模块说明](VnaLog/README.md)，库的公开采用证据见[选型调研](docs/research/C++日志库选型.md)。

## 使用 Web 验证

面板采用 ZNB8 的窄工具栏、黑色网格绘图区、右侧软工具与分组功能键、底部菜单状态栏。基准为 1280 × 800，绘图区/软工具/硬键宽度为 848/272/160 px；窗口按比例缩放，Esc 收起软工具并扩大绘图区。首次连接在控制权空闲时尝试取得租约。然后在 HOLD 下编辑 Start / Stop / Points / IFBW 并 Apply，等待设备完成且读回配置后，在 Sweep 中按 Single sweep。Continuous 启动连续扫描，Hold 等待当前完整帧收尾。Meas 的 S-Params 矩阵直接修改活动迹线的 S11/S12/S21/S22，保留名称和目录位置；Wave 提供六个快捷波量及 More Wave Quantities 的任意支持端口选择。新增/复制、删除和显示集合放在 Trace Config；Add Trace 复制活动测量并自动命名 Trc<n>。CONT 中可修改活动测量、复制或删除迹线，无需手动 HOLD。已有采集计划覆盖所需波量时直接复算当前完整帧；缺少源端口或接收机时，后台在完整帧边界提交新计划，等待匹配的新帧。

选中、显示集合、布局、格式、刻度和 Marker 由 C++ 统一保存，另一观察窗口和刷新后可见；前端只持有只读快照和未提交草稿。点击图内迹线条选择当前迹线，Format / Scale / Marker / Trace Config 的格式、刻度、M1 和可见性分别属于该迹线。显示支持对数幅度、线性幅度、相位、实部、虚部与实虚部叠加，以及 S11/S22 的 Smith 图、标记点、缩放及 CSV 导出。初始对数刻度为 −90…10 dB、Ref 0 在第 9 格，Autoscale 才按实际数据放大。网格铺满图窗，纵轴刻度叠在网格内，Start/Stop 在底部黑色通道行；`▼ 图号` 菜单切换全部图窗、当前迹线最大化或同窗叠加，并添加 M1。手册原始界面图和网上实机照片见[参考图档案](docs/reference/znb8/README.md)。S11=b1/a1、S21=b2/a1 来自源端口 1，S12=b1/a2、S22=b2/a2 来自源端口 2；每个比值的分子与参考来自同一源帧；结果无量纲，仪器状态与迹线提示明确标为“未校准”。原始 A/B 波仍标为 RAW，对数幅度相对于 `1 a.u.`，没有改成 S 参数或校准数据。

测量目录最多 32 项，显示集合最多 4 条，可在 Trace Config 切换 Display On/Off；选择目录后面的测量时按显示槽位替换，不删除测量定义。CSV 和网页显示最多 2001 个均匀选取的真实频点，超过时明确显示原始点数与展示点数；SCPI 仍返回完整数组。无帧、旧配置帧、断线与故障均显示状态，不生成演示曲线。保留旧帧时频轴和点数来自该帧，IFBW 显示 `—`，避免将新配置值标在旧数据上。校准、RF 开关、预设等未实现入口禁用。Smith 图只用于反射参数 S11/S22，直接以实际复数 Γ 投影；阻抗显示为归一化 z=(1+Γ)/(1−Γ)，不假定物理参考阻抗。|Γ|>1 不夹紧，Γ=1 显示开路状态，保留有效的原始 Γ。每条迹线独立保存格式与尺度，Smith 和直角坐标混合时分窗显示。

需要观察采集变化时，在启动服务前设置 `$env:VNAPRO_SIM_MODE = 'noise'` 和 `$env:VNAPRO_SIM_SEED = '42'`；DLL 在每次成功采集时加入最多 0.2% 的可重放复数噪声。Continuous 配合 Autoscale 可查看细微变化，HOLD 后数据固定。默认 `golden` 保留固定数值用于验收，模式在设备 Init 时冻结。完整验收场景、结果与 ThreadSanitizer 复验命令见[模拟设备采集验收](docs/模拟设备采集验收.md)。

Autoscale 由 C++ 按执行时当前迹线的完整原始帧适配一次并固定刻度；Continuous 后续帧只更新曲线，不改变每格刻度、参考值或纵轴标签。再次点击才重新适配，其他迹线保持各自刻度；无有效显示点或无法得到有限、非零范围时禁用 Autoscale，保留原刻度。

从 CLion 启动时，在 `vna_server` 的运行配置中，将 Environment variables 填为 `VNAPRO_SIM_MODE=noise;VNAPRO_SIM_SEED=42`，两项用分号分隔，值不带引号或前后空格；也可打开变量表格，分别填写名称和值。模式 `noise `（末尾含空格）会使模拟器初始化返回 `1`（参数无效）。设置完成后停止并重新运行服务。只在另一个 PowerShell 窗口设置变量，不会改变已经运行的 CLion 或服务进程。网页重新连接后按 Continuous，等新帧出现，再按 Autoscale；固定数据模式下得到的微小刻度不适合噪声模式。

Sweep 面板的 Run 是本次运行编号，连续采集期间保持不变；底部状态栏的 `F` 是帧序号，完整帧发布后才递增。判断采集是否推进，应观察 `F`。在默认 `golden` 模式中，帧序号递增而曲线保持相同属于预期行为。

Web 与 SCPI 共用唯一写控制权：其他网页及 SCPI 连接可只读观察。SCPI 最多 8 个连接，已有控制者时新连接固定为观察者；写请求或读清查询会关闭观察连接，不执行混合消息的前缀。Web token 的 30 秒租约由页面每 10 秒续租，释放或到期后先排空已接受有限操作，再允许接管。接管者继承实际扫描状态；修改频率、点数和 IFBW 时先 HOLD，修改测量目录可保持 CONT。Web 观察不消费 SCPI 错误队列。开发时运行 `WebUI` 中的 `npm run dev`，访问 `127.0.0.1:5173`，Vite 将 `/api` 代理到 `127.0.0.1:8080`。详细分层、使用与限制见 [前端说明](WebUI/README.md) 和 [服务设计](docs/网分服务与远程控制设计.md)。

## 使用 TCP SCPI 验证

控制权空闲时，一个 TCP 连接取得写控制权，其余连接只读；观察连接不会自动升级，可重新连接取得空闲控制权。每条程序消息以 LF 或 CRLF 结束；设置命令无确认响应，多条查询响应以分号分隔并以 LF 结束。可以用 socket 或 VISA TCPIP SOCKET 客户端发送：

```text
*IDN?
*CLS
SENS1:SWE:MODE HOLD;*OPC?
SENS1:FREQ:STAR 10MHZ
SENS1:FREQ:STOP 8GHZ
SENS1:SWE:POIN 201
SENS1:BAND 1KHZ
CALC1:PAR:DEF:EXT "wave_a","a1,1"
CALC1:PAR:DEF:EXT "wave_b","b1,1"
CALC1:PAR:DEF:EXT "Reflect","S11"
CALC1:PAR:DEF:EXT "Transmit","S21"
CALC1:PAR:DEF:EXT "Reverse","S12"
CALC1:PAR:DEF:EXT "Reflect2","S22"
TRIG:SOUR IMM
SENS1:SWE:MODE SING;*OPC?
SYST:ERR?
SENS1:X?
CALC1:PAR:SEL "wave_a"
CALC1:DATA? RDATA
CALC1:PAR:SEL "wave_b"
CALC1:DATA? RDATA
CALC1:PAR:SEL "Reflect"
CALC1:DATA? RDATA
CALC1:PAR:SEL "Transmit"
CALC1:DATA? RDATA
```

先等 `SING;*OPC?` 返回 `1` 并检查错误，再读频轴和曲线。`SENS1:X?` 返回 N 个 Hz；每条 RDATA 返回所选测量的 2N 个实部/虚部，可为原始接收波或未校准 S11/S21/S12/S22。单次完成后进入 HOLD；只切换选中测量不会改变完整帧。参考幅度 ≤ `1e-12 a.u.`、非有限样本或最终比值溢出时，Web 标记无效点，SCPI 拒绝整条数组并记录 `-230`。S 参数的单位、归一化、分层及验收见[测量设计](docs/未校准S参数测量设计.md)。

后台配置由值复制，采集按所有需求源端口完成一轮才发布不可变快照。成功配置或测量目录变更增加逻辑 `revision`，仅成功下发采集计划改变 `acquisitionRevision`。帧的 `revision` 保留实际采集计划版本，`measurementRevision` 标记本次复算使用的目录；前端同时核对这两种版本，旧帧不会冒充当前配置的结果。HOLD 等待正在进行的一轮完成；断开 TCP 不撤销已接受操作，新控制连接须等待这些操作终结。

## 模块与范围

| 目录 | 职责 |
| --- | --- |
| `VnaNativeSimulator` | TDC1VN08 旧接口模拟库 `tdc1vn08_drv_sim`、确定性信号及独立合同测试 |
| `VnaBoardSimulator` | TDC1VN10 新接口模拟库 `tdc1vn10_drv_sim`、异步多源扫描及分段取数 |
| `VnaLog` / `config/vnapro.yml` | spdlog 日志门面、线程安全滚动目标与启动配置 |
| `VnaCore/include/vna_core` | 配置、不可变完整帧、异步命令结果与状态视图 |
| `VnaCore/src` | 命令 FIFO、单 worker 生命周期、Native 加载与帧转换 |
| `VnaMeasurement` | 测量定义、同帧原始波提取、未校准复数比值及逐点有效性 |
| `VnaDisplay` | 共享显示状态、格式投影、一次 Autoscale、原始点及插值 Marker |
| `VnaService` | 物理配置、测量目录、显示命令、控制权、有限操作及仪器错误状态 |
| `VnaServer/src/scpi` | 标准命令解析、命令映射、同步屏障、ASCII 响应 |
| `VnaServer/src/scpi_tcp_server.*` | TCP 分包/粘包、连接准入、部分发送和网络预算 |
| `VnaServer/src/network` | SCPI/Web 共用的平台 Socket 操作、网络初始化与句柄生命周期 |
| `VnaServer/src/web` | 有界 HTTP、静态资源、类型化 API、租约与操作结果 |
| `WebUI/src/api` | 类型化 HTTP、严格响应解码、服务签发的会话 token |
| `WebUI/src/composables` | 后端只读快照、命令完成读回与控制租约，不维护仪器真值 |
| `WebUI/src/components` | 仪器外壳、软工具、绘图组件与共享输入 |
| `WebUI/src/plot` | 纯显示换算、Canvas 投影、真实频点标记和 CSV |
| `WebUI/src/styles` / `WebUI/test` | 视觉变量、外壳/软工具样式与数据、并发、绘图合同测试 |
| `WebUI/dist` | Vite 构建的静态部署产物，服务默认从这里加载 |
| `VnaServer/tests` | 实际 SCPI / HTTP 端到端验证与有序关闭 |
| `docs` | 核心设计与服务设计，含结构图和活动图 |

首期仅一个逻辑通道、一个控制者、线性扫描和 SCPI ASCII。频率、点数、IFBW 等物理扫频配置变更限定 HOLD；测量定义增删改在 Ready/Running 下串行执行，已有有限采集任务完成后再处理排队修改；忙时 INIT 不提供 PNA 的中止重启语义。`INIT:CONT OFF` 仅选择 MAN 触发源，不能代替 HOLD。S 参数现开放未校准 S11/S21/S12/S22，Smith 用于反射参数；`ABOR`、`*RST`、`SYST:PRES`、SDATA、校准及二进制块尚未实现，收到不支持的命令进入标准错误队列。Web 的界面布局参考 ZNB8，远程 SCPI 仍保持 Keysight 命令子集，不因此改为罗德命令。

网络预算为每消息 16 KiB、每连接待发数据 1 MiB，慢发送超过 10 秒关闭连接；模拟点数上限 10001。错误 FIFO 和命令队列有界。驱动 ABI 没有取消入口，因此无法对阻塞的真实 I/O 承诺限时退出；核心始终等到 worker 收尾后释放缓冲区和动态库。

Web 采用 HTTP/1.1 有界非阻塞收发，最多 8 个连接、8 KiB 请求头、16 KiB 请求体和 4 MiB 单响应；静态文件与 API 同源。JSON 输入和字符串输出严格验证 UTF-8；编码失败后必须 Reset，静态文件读写缓冲不一致或读取失败会关闭句柄。原子配置带 expectedRevision，有限写请求使用 requestId 去重并返回 operationId，不把 HTTP 202 当成设备成功。

此前采集验收在 Windows MinGW/GCC 10.3 下通过 12 项 CTest，WSL/GCC 13.3 全目标 ThreadSanitizer 同样通过且未报告数据竞争，覆盖完整 IQ golden、11 组核心时序/并发场景、无效样本及噪声贯通。18 项前端合同测试、严格类型检查与生产构建通过，其中两项检查窗口缩放、DPR 和样本投影；Chromium 实际交互验证覆盖配置、四条波形、逐迹线格式/参考位置、扫描控制、M1、三布局菜单、CSV、多会话与断线恢复。本次按职责整理后的正式代码在 Windows 全量 12 项 CTest 通过（13.87 秒），WSL 全目标 ThreadSanitizer 12 项通过（9.98 秒），70 个正式头单独 C++14 编译通过且无头文件循环依赖。临时实际分配故障验证通过 Claim 15 个、单次写入 37 个、结果 Poll 5 个分配点，并用恢复旧危险顺序的副本确认断言可检出错误；随后命名空间统一后的终检另行补记。分阶段证据见[模拟设备采集验收](docs/模拟设备采集验收.md)。

模拟 profile 明确采用整数 Hz → raw frequency、逻辑 aN/bN → Native A/B、9N 零值 opaque 表。真实驱动的单位、接收器及功率表合同尚未确认，服务启动会拒绝非 VnaPro 模拟器身份，避免误用该约定。

设计见 [核心采数更新软件设计](docs/网分核心采数更新软件设计.md) 和 [服务与远程控制设计](docs/网分服务与远程控制设计.md)。命令参照 [Keysight 官方 SCPI 文档](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Common_Commands.htm)，具体白名单与兼容限制见服务设计。

共享仪器显示状态、完整帧投影、跨协议同步和显示写入活动图见[共享显示域设计](docs/共享显示域设计.md)。
