# VnaPro

实现模拟设备的完整采数链路：Native 动态库 → 单 worker 采集核心 → 仪器服务 → TCP SCPI / Web。支持 Keysight N52xxB PNA/PNA-X 的已核对命令子集，Web 界面参考罗德与施瓦茨 ZNB8 的仪器布局；设备身份如实返回 VnaPro，当前后台只允许本项目模拟器。

## 构建和测试

Web 前端是独立的 Vue 3 + TypeScript + Vite 工程，需要 Node.js ≥ 22.13 和 npm。服务需要 CMake ≥ 3.13、C++14 编译器、线程库；网络集成测试使用 Python 3 标准库。Windows MinGW 的 `bin` 必须加入 PATH。在项目根目录先构建前端，再构建服务：

```powershell
Set-Location frontend
npm.cmd ci
npm.cmd test
npm.cmd run build
Set-Location ..
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=I:/mingw64/bin/g++.exe
cmake --build build --parallel
cmake -E chdir build ctest --output-on-failure
```

替换编译器路径。Linux/macOS 可去掉 MinGW 编译器参数，使用当地工具链。独立模拟器构建仍见 [VnaNativeSimulator/README.md](VnaNativeSimulator/README.md)。本次验证环境为 Windows MinGW/GCC 10.3；Linux、macOS 和既定 GCC 7.3 发布门禁需在对应环境复验。

`npm run build` 包含 Vue/TypeScript 类型检查，产物在 `frontend/dist`。只构建或运行 SCPI 服务可跳过 Node/npm 步骤，并以 `--no-web` 启动；Web HTTP 集成测试需要先生成前端产物。前端开发与目录说明见 [frontend/README.md](frontend/README.md)。

## C++ 文件组织与格式

独立类和公开数据类型分别使用同名文件，头文件声明接口、源文件实现行为。类内部的私有状态记录保留在所属实现中，测试助手位于各模块的 tests，不进入生产接口。`CoreTypes.h` 仅聚合核心类型头；JSON 值、解析器和编码器各自独立，SCPI/Web 共用 `src/network` 的平台适配与 Socket 资源管理。

先正常编写代码，再直接运行 clang-format 程序，使用根目录 `.clang-format` 自动格式化。短函数体展开，类、结构体和函数定义之间自动留一空行（SeparateDefinitionBlocks: Always）；120 列由格式器处理，不为满足提交行数压缩代码。按功能拆分提交，不通过省略换行缩小差异。使用 clang-format 18.1.8，在根目录运行：

```powershell
$cppFiles = Get-ChildItem -Path VnaCore,VnaService,VnaServer -Recurse -File |
    Where-Object { $_.Extension -in '.h','.cpp' } | Select-Object -ExpandProperty FullName
clang-format --style=file -i $cppFiles
clang-format --style=file --dry-run --Werror $cppFiles
```

日常修改只需将本次变更的文件传给格式器。其他 C++ 模块同样使用根目录配置。

## 启动与测量

```powershell
.\build\bin\vna_server.exe --bind 127.0.0.1 --port 5025
```

默认加载构建生成的模拟动态库，并启动 SCPI `127.0.0.1:5025` 和 Web `http://127.0.0.1:8080`。静态根目录为构建时指定的 `frontend/dist`，浏览器打开 [Web 面板](http://127.0.0.1:8080/) 即可操作。迁移产物时显式传入 `--driver` 的模拟库绝对路径，并用 `--web-root` 指向部署后的 `frontend/dist`；不能把未构建的 `frontend/src` 作为静态根目录。`--port 0` / `--web-port 0` 自动选择空闲端口，启动输出 `LISTENING 地址:端口` / `WEB_LISTENING URL`。SCPI 与 Web 分别通过 `--bind` / `--web-bind` 指定 IPv4 地址，默认仅绑定本机；`--no-web` 关闭 Web 入口。

## 使用 Web 验证

面板采用 ZNB8 的窄工具栏、黑色网格绘图区、右侧软工具与分组功能键、底部菜单状态栏。基准为 1280 × 800，绘图区/软工具/硬键宽度为 848/272/160 px；窗口按比例缩放，Esc 收起软工具并扩大绘图区。首次连接在控制权空闲时尝试取得租约。然后在 HOLD 下编辑 Start / Stop / Points / IFBW 并 Apply，等待设备完成且读回配置后，在 Sweep 中按 Single sweep。Continuous 启动连续扫描，Hold 等待当前完整帧收尾。添加 a/b 接收器测量后重新采集，可以查看同一帧的多条曲线。

点击图内迹线条选择当前迹线，Format / Scale / Marker / Trace Config 的格式、刻度、M1 和可见性分别属于该迹线。显示支持对数幅度、线性幅度、相位、实部、虚部与实虚部叠加，以及标记点、缩放及 CSV 导出。初始对数刻度为 −90…10 dB、Ref 0 在第 9 格，Autoscale 才按实际数据放大。网格铺满图窗，纵轴刻度叠在网格内，Start/Stop 在底部黑色通道行；`▼ 图号` 菜单切换全部图窗、当前迹线最大化或同窗叠加，并添加 M1。手册原始界面图和网上实机照片见[参考图档案](docs/reference/znb8/README.md)。对数幅度为相对于 `1 a.u.` 的显示换算，原始 A/B 波没有经过 S 参数计算或校准。

CSV 和网页显示最多 2001 个均匀选取的真实频点，超过时明确显示原始点数与展示点数；SCPI 仍返回完整数组。无帧、旧配置帧、断线与故障均显示状态，不生成演示曲线。保留旧帧时频轴和点数来自该帧，IFBW 显示 `—`，避免将新配置值标在旧数据上。未实现的 Smith 图、校准、RF 开关、预设等入口禁用。

需要观察采集变化时，在启动服务前设置 `$env:VNAPRO_SIM_MODE = 'noise'` 和 `$env:VNAPRO_SIM_SEED = '42'`；DLL 在每次成功采集时加入最多 0.2% 的可重放复数噪声。Continuous 配合 Autoscale 可查看细微变化，HOLD 后数据固定。默认 `golden` 保留固定数值用于验收，模式在设备 Init 时冻结。完整验收场景、结果与 ThreadSanitizer 复验命令见[模拟设备采集验收](docs/模拟设备采集验收.md)。

Web 与 SCPI 共用独占控制权：SCPI 控制连接存在时，网页只读；网页持权时，新 SCPI 连接会被拒绝。Web token 的 30 秒租约由页面每 10 秒续租，释放或到期后先排空已接受有限操作，再允许接管。接管者继承实际扫描状态，需要改配置时先 HOLD。Web 观察不消费 SCPI 错误队列。开发时运行 `frontend` 中的 `npm run dev`，访问 `127.0.0.1:5173`，Vite 将 `/api` 代理到 `127.0.0.1:8080`。详细分层、使用与限制见 [前端说明](frontend/README.md) 和 [服务设计](docs/网分服务与远程控制设计.md)。

## 使用 TCP SCPI 验证

一个 TCP 连接取得仪器控制权，第二个连接被关闭。每条程序消息以 LF 或 CRLF 结束；设置命令无确认响应，多条查询响应以分号分隔并以 LF 结束。可以用 socket 或 VISA TCPIP SOCKET 客户端发送：

```text
*IDN?
*CLS
SENS1:SWE:MODE HOLD;*OPC?
SENS1:FREQ:STAR 1MHZ
SENS1:FREQ:STOP 100MHZ
SENS1:SWE:POIN 201
SENS1:BAND 1KHZ
CALC1:PAR:DEF:EXT "wave_a","a1,1"
CALC1:PAR:DEF:EXT "wave_b","b1,1"
TRIG:SOUR IMM
SENS1:SWE:MODE SING;*OPC?
SYST:ERR?
SENS1:X?
CALC1:PAR:SEL "wave_a"
CALC1:DATA? RDATA
CALC1:PAR:SEL "wave_b"
CALC1:DATA? RDATA
```

先等 `SING;*OPC?` 返回 `1` 并检查错误，再读频轴和曲线。`SENS1:X?` 返回 N 个 Hz；每条 RDATA 返回选中接收器的 2N 个实部/虚部。单次完成后进入 HOLD；只切换选中测量不会改变完整帧。模拟数据不代表校准后的 S 参数。

后台配置由值复制，采集按所有需求源端口完成一轮才发布不可变快照。配置成功后增加 revision；旧帧不会作为新配置的数据返回。HOLD 等待正在进行的一轮完成；断开 TCP 不撤销已接受操作，新控制连接须等待这些操作终结。

## 模块与范围

| 目录 | 职责 |
| --- | --- |
| `VnaNativeSimulator` | 九个 Drv* ABI、确定性模拟信号、独立合同测试 |
| `VnaCore/include/vna_core` | 配置、不可变完整帧、异步命令结果与状态视图 |
| `VnaCore/src` | 命令 FIFO、单 worker 生命周期、Native 加载与帧转换 |
| `VnaService` | 物理配置、测量目录、控制权、有限操作及仪器错误状态 |
| `VnaServer/src/scpi` | 标准命令解析、命令映射、同步屏障、ASCII 响应 |
| `VnaServer/src/ScpiTcpServer.*` | TCP 分包/粘包、连接准入、部分发送和网络预算 |
| `VnaServer/src/network` | SCPI/Web 共用的平台 Socket 操作、网络初始化与句柄生命周期 |
| `VnaServer/src/web` | 有界 HTTP、静态资源、类型化 API、租约与操作结果 |
| `frontend/src/api` | 类型化 HTTP、严格响应解码、服务签发的会话 token |
| `frontend/src/composables` | 设备读回/有限操作与逐迹线显示状态，各自维护一份真值 |
| `frontend/src/components` | 仪器外壳、软工具、绘图组件与共享输入 |
| `frontend/src/plot` | 纯显示换算、Canvas 投影、真实频点标记和 CSV |
| `frontend/src/styles` / `frontend/test` | 视觉变量、外壳/软工具样式与数据、并发、绘图合同测试 |
| `frontend/dist` | Vite 构建的静态部署产物，服务默认从这里加载 |
| `VnaServer/tests` | 实际 SCPI / HTTP 端到端验证与有序关闭 |
| `docs` | 核心设计与服务设计，含结构图和活动图 |

首期仅一个逻辑通道、一个控制者、线性扫描和 SCPI ASCII。配置和测量定义变更限定 HOLD；忙时 INIT 不提供 PNA 的中止重启语义。`INIT:CONT OFF` 仅选择 MAN 触发源，不能代替 HOLD。`ABOR`、`*RST`、`SYST:PRES`、SDATA/S 参数、校准及二进制块尚未实现，收到不支持的命令进入标准错误队列。Web 的界面布局参考 ZNB8，远程 SCPI 仍保持 Keysight 命令子集，不因此改为罗德命令。

网络预算为每消息 16 KiB、每连接待发数据 1 MiB，慢发送超过 10 秒关闭连接；模拟点数上限 10001。错误 FIFO 和命令队列有界。驱动 ABI 没有取消入口，因此无法对阻塞的真实 I/O 承诺限时退出；核心始终等到 worker 收尾后释放缓冲区和动态库。

Web 采用 HTTP/1.1 有界非阻塞收发，最多 8 个连接、8 KiB 请求头、16 KiB 请求体和 4 MiB 单响应；静态文件与 API 同源。原子配置带 expectedRevision，有限写请求使用 requestId 去重并返回 operationId，不把 HTTP 202 当成设备成功。Windows MinGW/GCC 10.3 下 12 项 CTest 全部通过，WSL/GCC 13.3 全目标 ThreadSanitizer 下同样通过且未报告数据竞争。本轮增加完整 IQ golden、11 组核心时序/并发场景、无效样本及噪声贯通验收。18 项前端合同测试、严格类型检查与生产构建通过，其中两项新增测试检查窗口缩放、DPR 和样本投影；Chromium 实际交互验证覆盖配置、四条波形、逐迹线格式/参考位置、扫描控制、M1、三布局菜单、CSV、多会话与断线恢复。

模拟 profile 明确采用整数 Hz → raw frequency、逻辑 aN/bN → Native A/B、9N 零值 opaque 表。真实驱动的单位、接收器及功率表合同尚未确认，服务启动会拒绝非 VnaPro 模拟器身份，避免误用该约定。

设计见 [核心采数更新软件设计](docs/网分核心采数更新软件设计.md) 和 [服务与远程控制设计](docs/网分服务与远程控制设计.md)。命令参照 [Keysight 官方 SCPI 文档](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Common_Commands.htm)，具体白名单与兼容限制见服务设计。
