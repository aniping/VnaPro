# VnaPro

首期实现模拟设备的完整采数链路：Native 动态库 → 单 worker 采集核心 → 仪器服务 → TCP SCPI。支持 Keysight N52xxB PNA/PNA-X 的已核对命令子集，设备身份如实返回 VnaPro；当前后台只允许本项目模拟器。

## 构建和测试

需要 CMake ≥ 3.13、C++14 编译器、线程库。TCP 集成测试使用 Python 3 标准库；Windows MinGW 的 `bin` 必须加入 PATH。在项目根目录执行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=I:/mingw64/bin/g++.exe
cmake --build build --parallel
cmake -E chdir build ctest --output-on-failure
```

替换编译器路径。Linux/macOS 可去掉 MinGW 编译器参数，使用当地工具链。独立模拟器构建仍见 [VnaNativeSimulator/README.md](VnaNativeSimulator/README.md)。本次验证环境为 Windows MinGW/GCC 10.3；Linux、macOS 和既定 GCC 7.3 发布门禁需在对应环境复验。

## 启动与测量

```powershell
.\build\bin\vna_server.exe --bind 127.0.0.1 --port 5025
```

默认加载构建生成的模拟动态库。迁移产物时显式传入 `--driver` 的模拟库绝对路径；`--port 0` 自动选择空闲端口，启动输出 `LISTENING 地址:端口`。默认仅绑定本机，需要远程连接时明确指定绑定地址。

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
| `VnaServer/tests` | 实际 TCP 端到端验证 |
| `docs` | 核心设计与服务设计，含结构图和活动图 |

首期仅一个逻辑通道、一个控制连接、线性扫描和 ASCII。配置和测量定义变更限定 HOLD；忙时 INIT 不提供 PNA 的中止重启语义。`INIT:CONT OFF` 仅选择 MAN 触发源，不能代替 HOLD。`ABOR`、`*RST`、`SYST:PRES`、SDATA/S 参数、校准、二进制块及 Web 尚未实现，收到不支持的命令进入标准错误队列。

网络预算为每消息 16 KiB、每连接待发数据 1 MiB，慢发送超过 10 秒关闭连接；模拟点数上限 10001。错误 FIFO 和命令队列有界。驱动 ABI 没有取消入口，因此无法对阻塞的真实 I/O 承诺限时退出；核心始终等到 worker 收尾后释放缓冲区和动态库。

模拟 profile 明确采用整数 Hz → raw frequency、逻辑 aN/bN → Native A/B、9N 零值 opaque 表。真实驱动的单位、接收器及功率表合同尚未确认，服务启动会拒绝非 VnaPro 模拟器身份，避免误用该约定。

设计见 [核心采数更新软件设计](docs/网分核心采数更新软件设计.md) 和 [服务与远程控制设计](docs/网分服务与远程控制设计.md)。命令参照 [Keysight 官方 SCPI 文档](https://helpfiles.keysight.com/csg/N52xxB/Programming/GP-IB_Command_Finder/Common_Commands.htm)，具体白名单与兼容限制见服务设计。
