# VnaPro Web 前端

独立的 Vue 3 + TypeScript + Vite 工程，用于验证模拟器的真实采数、配置更新和控制竞争。界面参考 ZNB8 的仪器布局，显示 RAW A/B 接收波和未校准 S11/S21/S12/S22；S11/S22 支持 Smith。界面风格不代表具备罗德硬件或完整 ZNB 功能，SCPI 仍使用已实现的 Keysight 命令子集。

## 安装、构建与运行

需要 Node.js ≥ 22.13 和 npm。在本目录执行；Windows PowerShell 使用 `npm.cmd`，其他环境可使用 `npm`：

```powershell
npm.cmd ci
npm.cmd test
npm.cmd run build
```

`package-lock.json` 固定依赖。`npm run build` 先执行 `vue-tsc --noEmit`，再生成 `dist/index.html` 和 `dist/assets`。随后回到仓库根目录构建并运行 C++ 服务，默认访问 [http://127.0.0.1:8080](http://127.0.0.1:8080/)；完整命令见 [根 README](../README.md)。部署时将整个 `dist` 目录交给服务的 `--web-root`，并用 `--driver` 指定部署后的模拟 DLL 绝对路径。浏览器运行静态产物，不需要安装 Node.js。

开发时保持 C++ 服务运行在 `127.0.0.1:8080`，另外执行：

```powershell
npm.cmd run dev
```

访问 [开发页面](http://127.0.0.1:5173/)；Vite 对 `/api` 的请求代理到 `127.0.0.1:8080`，无需跨域配置。`vite.config.ts` 的开发端口固定为 5173。`npm run preview` 仅预览构建的静态页面，没有 `/api` 代理，完整采数验收应从 C++ 服务加载 `dist`。

## 目录与依赖

```text
WebUI/
├─ package.json / package-lock.json  依赖与开发、测试、构建脚本
├─ vite.config.ts                   开发代理及 ES2022 构建
├─ tsconfig*.json                   产品与测试的类型检查配置
├─ index.html / src/main.ts         Vue 挂载入口
├─ src/
│  ├─ App.vue                       装配、软工具路由、画布缩放、下载
│  ├─ api/
│  │  ├─ types.ts                   设备快照、帧、操作的类型合同
│  │  ├─ decoders.ts                严格校验 JSON、身份、频轴和数组
│  │  └─ instrumentApi.ts           HTTP、请求超时、session、requestId
│  ├─ composables/
│  │  ├─ useInstrument.ts           轮询、租约、操作完成与设备读回
│  │  └─ useWorkspace.ts            活动迹线及逐迹线可重建显示状态
│  ├─ domain/
│  │  ├─ presentation.ts           格式、刻度、标记、布局及迹线颜色
│  │  ├─ measurements.ts           四个 S 参数/RAW 选择及测量状态文本
│  │  └─ sweep.ts                  服务端能力范围内的扫描草稿校验
│  ├─ components/
│  │  ├─ instrument/               工具栏、硬键、菜单状态栏、图标
│  │  ├─ softtools/                配置、测量、扫描、迹线、系统面板
│  │  ├─ diagrams/                 图布局、迹线条、Canvas 容器、标记读数
│  │  └─ shared/FrequencyField.vue  数值与 Hz/kHz/MHz/GHz 单位输入
│  ├─ plot/
│  │  ├─ traceMath.ts              复数显示换算、刻度、最近实际频点
│  │  ├─ canvasRenderer.ts         坐标投影、网格、曲线与参考标记
│  │  ├─ smithMath.ts              Γ 投影、二维采样吸附、归一化阻抗
│  │  ├─ smithRenderer.ts          圆形 Smith 网格、曲线与缩放
│  │  ├─ formatting.ts             数值、频率和单位文本
│  │  ├─ exportCsv.ts               当前真实显示帧的 CSV
│  │  └─ index.ts                   绘图模块对组件的接口
│  └─ styles/                      tokens、外壳和软工具样式
├─ test/                           设备状态/竞争与绘图/CSV 合同测试
├─ scripts/test.mjs                编译测试，再用 Node 测试运行器执行
└─ dist/                           构建产物；不提交到 Git
```

依赖向下收敛：组件通过事件交给 `App` 装配，设备写入走 `useInstrument → InstrumentApi`；显示设置走 `useWorkspace`。绘图模块只处理已获得的数据，不调用驱动或启动采集。

```mermaid
flowchart TB
  Entry["main.ts / App.vue<br/>装配与缩放"] --> UI["components<br/>外壳、软工具、绘图与共享输入"]
  Entry --> Device["useInstrument<br/>设备状态与有限操作"]
  Entry --> Workspace["useWorkspace<br/>逐迹线显示状态"]
  Entry --> Styles["styles<br/>视觉变量及外壳样式"]
  UI --> Plot["plot<br/>显示换算、Canvas、标记与 CSV"]
  Workspace --> Presentation["domain/presentation<br/>可重建显示合同"]
  Plot --> Presentation
  Plot --> Types["api/types<br/>真实帧合同"]
  Device --> Api["api/instrumentApi + decoders<br/>严格解码与会话"]
  Api --> Types
  Api --> Http["C++ HTTP / InstrumentService<br/>权威配置、控制权及采集"]
```

## 状态与竞争处理

`api` 只负责传输和解码。run/revision/frame/operation 的 uint64 身份保留为十进制字符串，避免 JavaScript 精度丢失；频轴、有效位及实虚部长度不符合合同时拒绝该响应。每条测量和帧迹线必须明确携带 `kind`、`unit`、`calibration`，并与 `parameter` 及端口一致：RAW 为 `receiver / a.u. / raw`，四个 S 参数为 `sParameter / 1 / uncalibrated`。缺失或矛盾的元数据直接拒绝，不在前端猜测单位或校准状态。状态必须携带 `capabilities`：端口数、最小/最大频率与扫描点数；配置和 RAW 端口选择由能力约束。默认仪器为四端口、10 MHz–8 GHz、201 点；二端口配置只显示端口 1、2。token 由服务签发，保存在当前页面的 `sessionStorage`；不会在前端自行生成控制权。

`useInstrument` 维护一个不重叠的轮询周期。在线约每 200 ms 读取状态及帧，断线后约每 1 s 重试；一个周期完成后再调度下一次。控制权空闲时初次连接尝试取得租约，持权页面每 10 s 续租服务的 30 s 租约。释放意图立即停止续租，旧的持权响应不能让页面重新自动抢占。

配置与测量定义携带 `expectedRevision`，修改限制在 HOLD。写入返回 HTTP 202 仅表示受理：保留 operationId 和受理时的原始 token，观察到完成后再次读取权威状态，再更新生效配置。释放或换 token 后也按原 token 观察已受理操作；遇到不确定的网络失败不会自动重发写入。停止界面轮询只终止 HTTP 观察，不宣称取消设备已接受的任务。

`useWorkspace` 仅保存活动迹线、布局，以及每条迹线的 Format/Scale/M1/可见性。它不复制设备配置或采集状态机；改变显示设置不会增加仪器 revision 或重复触发扫描。配置输入是草稿，轮询不能覆盖尚未应用的编辑。

测量目录最多 32 项，显示集合最多四项。Trace Config 的 Display 开关只改变本地显示集合；选择尚未显示的测量会加入集合，满四项时替换当前活动显示槽位，不删除任何测量定义。Meas 使用带名称的 PUT 原位更新活动迹线，保留名称、目录顺序和显示槽位，不通过删除再新增完成切换。帧请求按 `GET /api/v1/frame?trace=<编码名称>&trace=...` 指定集合；空集合使用 `?display=none`。切换后清理不再显示的旧迹线、立即刷新，并按显示选择版本丢弃旧请求的迟到结果。仪器写入及控制权的竞争规则保持独立。

Scale 和 Marker 输入按活动迹线及已应用显示设置同步；200 ms 帧轮询即使返回新的对象，也不能重置用户尚未应用的刻度或频率草稿。

工具栏和 Scale 面板的 Autoscale 共用一次性操作：按点击时当前迹线的显示帧计算并保存具体范围。后续帧、状态轮询和布局切换沿用该范围，Per Div、Ref、纵轴刻度及未应用的 Scale 草稿不会随噪声改变；再次点击才重新适配。手动 Apply Scale 或切换 Format 会更新已应用刻度。无有限显示值或拟合范围无效时禁用两个入口，保留原刻度。极小范围的 `Δ` 刻度按范围判断，手动设置也可使用，与持续自动缩放无关。

## 操作与显示

基准画布为 1280 × 800。工具栏高 42 px，底部菜单状态栏高 32 px；主区 848 px、软工具 272 px、右侧两列硬键 160 px。软工具的标签列在右侧，宽 70 px，文字横排。视口改变时整体等比缩放；按 Esc 收起软工具，主区扩大到 1120 px，点击硬键重新打开。

新服务启动后已有活动迹线 `Trc1 / S21`，使用仪器配置的完整频段、默认 201 点；单端口配置使用 S11。启动状态为 HOLD，无初始采集帧，图中显示网格和“无帧”状态；执行 Single sweep 或 Continuous 才获得真实曲线。浏览器刷新读取已有设备目录和配置，不重新创建 RAW a1,1，也不覆盖其他客户端已选择的测量。

1. 在 Setup 或底栏取得控制权；若 SCPI 或另一页面持权，本页可只读查看。
2. 在 HOLD 下打开 Start/Stop/Center/Span 或 Power/Bw/Avg，编辑能力范围内的频率、点数和 IFBW，Apply 后等待设备完成及读回。允许 Start = Stop，或把 Span 设为 0，进行 CW 定频采样；返回频轴允许同一频率重复。
3. Meas 的 S-Params 页直接选择 S11/S12/S21/S22，修改活动迹线的测量量，不要求输入新名称。S11/S21 在源端口 1 的同帧波量上计算 `b1/a1`、`b2/a1`；S12/S22 在源端口 2 的同帧波量上计算 `b1/a2`、`b2/a2`。Wave 页提供六个标准两端口波量快捷选择；More Wave Quantities 对话框可按仪器能力指定 a/b、接收端口和激励端口，确认后修改活动迹线。超出当前端口能力的快捷选择禁用。定义改变后重新采集。
4. Sweep 的 Single sweep 采一帧并进入 HOLD；Continuous 连续采集，Hold 等待当前完整帧收尾。Manual trigger 仅在实际 MAN 触发并已 armed 时启用。
5. 点击图内迹线条选择当前迹线。Format、Scale、Marker 改变本地显示。Trace Config 管理设备测量目录和本地 Display 集合：Add Trace 复制活动测量定义并自动命名 `Trc<n>`，Delete active 删除设备定义，Display 选择本地可见集合；设备操作要求持有控制权并处于 HOLD。
6. 在 File/Print、Tools 或工具栏导出当前显示的 CSV。

图窗采用 10 × 10 网格，网格铺满绘图区，仅留 1 px 边线；纵轴数值和单位叠在网格内，普通刻度使用 `#4b5f6c`，如 `10dB` 不增加数值与单位之间的空格。频率 Start/Stop 显示在 20 px 黑色通道行，`Bw` 使用实际 IFBW，扫频点数使用真实配置或当前帧；不填充未实现的 Pwr。18 px 迹线条紧凑排列，如 `Trc1 S21 dB Mag 10 dB/ Ref 0 dB`，当前迹线选中呈 `#0d75bf` 蓝底。图内文字使用 Tahoma/Arial 近似参考截图，未确认原机字体。每条可见迹线保留自己的参考虚线与右侧同色向左三角；当前坐标所对应迹线的 Ref 虚线及对齐刻度用浅色 `#dbe2e6` 强调，其他叠加迹线的参考虚线使用自己的颜色。M1 使用小填充三角；右上角读数为实心 `#323f48` 背景、白色数值，圆点用迹线色指示活动标记。频率以工程单位显示六位小数，对应的实际采样索引不变，高精度提示仍可读取。靠近顶部时三角反向、文字移到下方；频率端点处目标向内收，尖点仍对应真实采样位置，单击不会跳频。

刻度文字直接叠在绘图区，不添加半透明背景。Canvas 位图按最终屏幕尺寸和设备像素比生成，显示边界也对齐到物理像素，避免非整数缩放时再次插值；绘图偏移补偿保持样本和 M1 在原绘图区的实际位置。网格、边线和参考虚线占 1 个物理像素，曲线保持 1 个屏幕 CSS 像素宽，不因选中或整机缩放加粗。视口尺寸或设备像素比变化时重新绘制，包括 HOLD 状态。曲线保留真实样本及无效点断线，不通过平滑或滤波改变采集数据。

图窗右上角的 `▼ 图号` 菜单支持 All Diagrams（显示集合每条测量一个图窗，最多四个）、Maximized（当前迹线独占主区，保留原图号）、Overlay All（按坐标类型叠加）和 Add Marker（当前迹线的 M1）。矩形与 Smith 混合时自动拆成两窗，避免把复平面与频率轴混画。工具栏最大化按钮切换 All Diagrams/Maximized；菜单支持方向键、Home/End、Enter、Esc 和外部点击。只有显示集合中的真实测量才生成图窗，不额外填充空窗。配色按每个图窗重新开始：独立图窗的首条为绿色，叠加时按绿、黄、青、紫区分；这是本项目选定配色，ZNB 手册允许用户配置颜色和字体。

初始对数刻度按手册示例采用 −90…10 dB、10 dB/div、Ref 0 dB 在第 9 格。Autoscale 才放大实际数据的细小变化；极小变化采用 `Δ` 刻度，迹线条和刻度提示保留真实 Ref。Scale 的参考位置从底部 0 到顶部 10；M1 点击/拖动或在 Marker 中输入频率，始终落在 API 返回的最近实际频点。聚焦图区后，←/→ 移动一点，Shift 移动十点，Home/End 到两端。六种格式为 dB Mag、Lin Mag、Phase、Real、Imag、Real/Imag；RAW dB 参考 1 a.u.，S 参数为 `20 log₁₀ |S|`，线性幅度和实虚部使用无量纲单位 `1`，相位使用度数。RAW 迹线条保留 RAW 标签；S 参数的“未校准”状态在全局状态、测量提示及可访问文本中可见，避免重复占满迹线标题。尚未应用误差校准，不标成 dBm。

复数点的 `valid` 表示后端采样或比值计算有效，与当前格式能否给出有限显示值不同。有效的 `0+0i` 在线性幅度下为 0，dB 为 −∞，相位未定义；相位曲线留空，M1 显示 `—`，CSV 保留实虚部 0 和 `valid=true`、显示值留空。dB 计算先缩放分量，避免有限实虚部的中间线性幅度溢出；两分量均为 `Number.MAX_VALUE` 时仍能显示有限的 dB。

Smith 仅向反射 S11/S22 开放，按复数 Γ 直接投影，不在前端重复计算接收波比值。圆心是 Γ=0，单位圆是 |Γ|=1；`|Γ|>1` 保留真实坐标，不钳制或误标无效。宽屏、窄窗和非整数缩放保持圆形。Scale 的 Outer Magnitude 控制显示半径，最小为 0.01；Autoscale 根据当前有效点适配一次并固定半径，后续帧不能改变它。同一 Smith 图窗的所有曲线、M1 与点击吸附使用活动迹线半径；迹线条展示当前有效半径，提示同时列出各迹线保存值。切换活动迹线时整窗切换半径，独立图窗各自使用保存值，不能把同一 Γ 绘在共享网格的不同位置。矩形刻度与圆图半径各自保留；从 Smith 返回之前的矩形格式会恢复原刻度。

Smith M1 点击/拖动按二维屏幕距离选择最近的实际频点；方向键和输入频率仍使用真实频率序列。CW 同频点保留实际展示数组索引，使二维选择与方向键能够区分同频采样；新帧该索引频率改变时才重新按频率吸附。读数显示 Γ 与 `z=(1+Γ)/(1−Γ)` 的归一化阻抗，不假定 50 Ω 或其他物理参考阻抗，也不声称已校准。Γ=1 时原始点仍有效，阻抗显示 `∞ (open)`；数值超出可表示范围则单独显示转换溢出，不修改后端有效位。无效参考点保留断线。

所有图来自一个完整帧。无帧仍显示网格；断线、配置已变或新 run 尚未发布数据时保留旧帧并明确标记。旧帧的频轴和点数来自该帧，IFBW 显示 `—`，因为该 API 帧不携带采集带宽，不能用新配置冒充旧数据的带宽。

Web 最多返回 2001 个均匀选取的真实点，并显示原始/展示点数。M1 和 CSV 使用这些实际展示点，不重建被省略的点。参考波过小或无效时，后端将对应 S 参数点标为无效；图线断开、M1 显示 Invalid point，前端不补造比值。SCPI 读取完整扫描数组。误差校准、Polar、多标记、RF 开关、预设等尚未实现，相关入口禁用。

CSV 使用 schema 3，记录帧身份、抽样信息、有效位、可见迹线的 Re/Im 与格式值。schema 2 原有列保持原位，后追加 `gamma_real,gamma_imaginary,normalized_r,normalized_x,impedance_state`；仅 Smith 填写这些列。Smith 的标量 `display_value` 留空，避免把复数误写为单值；开路、转换溢出独立记录为 `open`、`overflow`，原始有效位不变。`unit` 是当前显示格式单位，`raw_unit` 是原始复数数组的单位（RAW 为 `a.u.`、S 参数为 `1`）。按列号读取旧格式的程序应先检查表头；导出中的 `uncalibrated` 表示未校准比值。

## 验证与界面依据

`npm test` 的 52 项合同测试覆盖严格解码、uint64 身份、202 等待/读回、旧响应与不确定写入竞争、释放后禁止续租、原 token 观察、过期租约以及实际频点标记、无效点断线、格式单位、Autoscale、参考位置、网格满幅、Marker 四角真实尖点和 CSV 来源；四个 S 参数的端口与元数据一致性、能力范围、显示集合及迟到帧竞争均有回归。本次增加 Meas 单次 PUT 原位更新并保留迹线身份、Add Trace 复制真实活动测量与自动命名、Marker 工程单位与六位小数显示回归。Smith 覆盖匹配/短路/开路/纯虚阻抗、极大值与溢出、二维实际点吸附、无效点断线、超单位圆、非方形 CSS 缩放与 DPR、一次性半径及矩形范围恢复、复数 CSV。Autoscale 回归通过工作区真实动作与绘图读取联测，覆盖连续帧固定范围、再次点击、迹线隔离、手动刻度及格式切换、无数据和无效拟合、实虚部共同范围及固定 Δ 刻度。另检查三种视口缩放与三种 DPR 组合的位图尺寸、样本投影和物理像素对齐。`npm run build` 负责全部 Vue/TypeScript 类型检查和生产构建。实际 HTTP/SCPI、静态部署与浏览器交互另由根目录服务集成测试和真实页面验收验证。

2026-10-10 的绘图复验使用独立模拟服务、seed 42 的 401 点噪声帧，覆盖 1280 × 800 / DPR 1、1440 × 900 / DPR 1、1024 × 640 / DPR 1、1280 × 800 / DPR 1.25 和 DPR 2。四图窗的网格横截面均为 1 列物理像素，颜色保持 `#4b5f6c`；刻度背景透明，HOLD 下改变视口仍正确重画。M1 与实际曲线样本的最大差为 0.042 个物理像素；显示操作未触发设备配置或扫描写入，冻结帧保持一致，页面无脚本错误。

布局依据为 [ZNB Getting Started v70，第 37–40 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/getting_started/1316_0062_01/ZNB_GettingStarted_en_70.pdf#page=37) 的工具栏、硬键、软工具及底部菜单，以及 [ZNB User Manual v74，第 90、94、803 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=90) 的迹线、参考线、标记和逐图配色规则。[参考图档案](../docs/reference/znb8/README.md) 保存 PDF 原始嵌入图、完整页面和网上实机照片，并注明来源及固件差异。参考项目 `I:\ai\cc\VectorNetworkAnalyzer\frontend` 只作只读比较，主要核对其网内刻度、紧凑迹线条及 Marker 布局；未移植其设备状态和通信逻辑。参考图片不作为页面资源发布，界面使用 VnaPro 自有文字与图标。
