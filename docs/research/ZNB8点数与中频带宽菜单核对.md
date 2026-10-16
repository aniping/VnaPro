# ZNB8 点数与中频带宽菜单核对

核对日期：2026-10-12。对象为用户截图中的 `Points`、`IF bandwidth` 两个控件，不扩展到尚未实现的校准、滤波器或平均功能。

## 结论

整改前把这两项合在 `Sweep settings` 中，**不符合本项目选定的 ZNB8 官方界面基线**：普通频率扫描的点数属于 `Sweep`，中频带宽属于 `Pwr Bw Avg`。两者都是通道采集参数，菜单分组不同不会改变后端的归属或权限规则。

| 参数 | 官方入口 | 官方控件名称 | User Manual v74 依据 |
| --- | --- | --- | --- |
| 扫频点数 | `Channel → Sweep → Sweep Params` | `Number of Points` | §5.10、§5.10.1，印刷页 533–534 / PDF 页 533–534 |
| 中频带宽 | `Channel → Pwr Bw Avg → Bandwidth` | `Bandwidth` | §5.9、§5.9.3，印刷页 528、530–531 / PDF 页 528、530–531 |
| 起止频率 | `Stimulus → Start / Stop`，打开 `Stimulus` 页签 | `Start Frequency`、`Stop Frequency` | §5.8、§5.8.1，印刷页 523–524 / PDF 页 523–524 |

上述对应关系来自适用型号包含 ZNB8 的[官方 User Manual v74](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=528)。点数控件原图位于[第 534 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=534)，带宽控件原图位于[第 531 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=531)。

## 需要保留的边界

- `Stimulus` 在 **CW Mode** 下也提供 `Number of Points`，§5.8.1 第 524–525 页明确这是 CW 扫描的入口；它不能作为将普通频率扫描点数移入 `Stimulus` 的依据。
- 带宽原图是数值输入、若干带宽快捷按钮和数字 IF 滤波器选择，右侧页签为 `Bandwidth`；本项目的单一下拉框仅是当前支持能力的简化表达。
- 这两个软工具的原图没有截图中的通用 `Apply / Reload` 按钮组合。数值编辑器通过确认键应用新值，参见 §3.3.6.2 第 59 页。`Apply / Reload` 是本项目的草稿提交与重读交互，不应描述为原机复刻；原机其他对话框可能有 `Apply`，例如校准向导。
- 本次只核对菜单和控件位置；资料不能替代本项目驱动的并发、配置生效和 `HOLD` 限制契约。

这些边界同样来自[官方 User Manual v74 第 59、524–525、531、534 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=59)。

## 已实施调整

2026-10-12 已在 `Sweep` 软工具增加 `Sweep Params` 页签，放置 `Number of Points`；`Power / Bw / Avg` 硬键改为路由到独立的 `Pwr Bw Avg` 软工具，带宽放在其 `Bandwidth` 页签。`App.vue` 分别维护频率、点数、带宽三个固定菜单实例，共用 `ChannelSettingsSofttool.vue` 的输入和提交组件；每个菜单仅发送本页字段，保留独立草稿及后端版本校验。`Sweep Control` 与 `Trigger` 分页。未实现的功率、滤波器类型与平均能力不呈现为可操作控件。前端仍只提交用户意图并显示后端状态，菜单调整不产生浏览器独立的采集配置。

## Sweep Control 显示与当前控制语义

2026-10-15 以 ZNB User Manual v74 为基线重新区分静态菜单、命令语义及尚未确认的 GUI 状态。不能从“模式、启动和触发来源是不同概念”推出“选择所有模式都必须保持停止”。也不能为缩短操作而向原机菜单添加入口。

| 核对项 | 官方明确依据 | 可据此实现的范围 |
| --- | --- | --- |
| Trigger In 控件 | 第 550 页原图 | Free Run、External、Multiple、Manual、Manual Trigger、Sequence、Delay、Signal Type、Trigger Manager；图中没有 Start/Stop |
| Continuous | 第 556、1245 页；第 918 页数据导入说明 | 激活连续测量；切回 Continuous 会请求新的硬件数据，不能实现为只选中后一直 HOLD |
| Single 与周期启动 | 第 556–557、1245–1246 页 | 达到 Sweeps 后停止；Restart/INIT 开始新周期；`INIT:CONT OFF` 示例是在当前扫描结束后停止 |
| Start Sweep | 第 556 页原图、第 557 页正文 | 原图按钮写 Start Sweep，正文称 Restart Sweep，作用为停止当前周期并开始新周期 |
| Stop Sweep | 第 557、1247 页 | 明确有停止正在进行的扫频的功能及 `INIT:STOP` 命令；第 556 页原图未显示第二个独立 Stop 按钮 |
| Manual Trigger | 第 551、913、1592 页 | 产生手动触发事件，作用于等待中的动作；Sequence=Sweep 时一个事件对应一次完整扫频 |
| 工具栏扫频图标 | 第 46 页 | Restart sweep in all channels，与 Sweep Control 的 Restart Sweep 对应，不能改成选择 Single 的快捷键 |

原图、控件和命令说明见[第 550–557 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=550)，工具栏见[第 46 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=46)。Sweep Control 按该静态原图保留一个固定标签的 Start Sweep，不添加独立 Stop 按钮，也不猜测 Start/Stop 随运行状态动态复用。Trigger In 取消本项目自行增加的启动、停止及跨页操作指引；未实现控件仍灰显并说明原因。

服务保存 `selectedSweepMode` 作为模式选择，`sweepMode` 继续报告实际 CONT/SING/HOLD，Single 完成不丢失选择。改选 Continuous 立即按当前触发源进入连续运行：IMM 自动采集，MAN 等待手动事件。Single 采用第 1245 页 `INIT:CONT OFF` 示例的合同，在当前完整帧结束后停止，再由 Start Sweep 开新周期。Start Sweep 与工具栏 Restart Sweep 均保留模式和触发源；Manual 下只布防。Manual/Continuous 每次触发后继续等待，Manual/Single 完成一帧后 HOLD，不由 Manual Trigger 隐式重开周期。

切源保留队首执行时的运行或停止意图，运行中使用完整帧 Stop/Start 屏障；已停止时只改源选择，重复选择保持幂等。MAN/Single 已布防时切 IMM 会完成一帧并 HOLD。以上是本项目在现有核心上的状态迁移和安全门控，不等于已经逐状态验证 ZNB 实机。

**资料边界：**第 556 页将 GUI 模式关联到 `INIT:CONT` 和 `SWE:MODE`；[第 1473 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=1473)又区分 SINGle（Single，正在扫描）与 HOLD（Single，未扫描），未说明点击 GUI Single 时具体执行哪个命令。第 551 页“非 Manual 时禁用”只证明来源是必要条件，不能证明闲置、Single 完成或忙时按钮的全部启停条件。手册也没有完整说明停止后切源、重复点击已选 Continuous、Stop 后模式单选变化，或 Start/Stop 是否共用一个动态按钮。当前没有可供核对的实机或操作记录，这些项目保留未验证标记；不据缺失的说明添加自动启动或额外快捷入口，也不声称完整复刻原机状态机。

新界面使用 `POST /api/v1/sweep-control`。既有 `/api/v1/operations` 保留兼容语义，其中 single / continuous 仍强制使用 Immediate；既有 SCPI 的 `SENS:SWE:MODE`、`INIT:CONT` 与 `INIT` 合同保持不变，不用新 Web 的选择动作重解释旧远程命令。

尚未实现的 `Sweeps` 多次扫描计数、`Restart Manager`、批量通道控制、`Sweep Controller`、External、Multiple、`Trigger Manager`、Point 逐点触发、Trigger Delay 和 Pipelining 保留灰显入口并解释限制。各通道可分别设置和运行；当前仅支持目标通道完整帧触发，不提供外部信号、逐点时序或延迟能力。布局参考 ZNB，不据此宣称具备原机全部扫描和触发能力。

2026-10-15 补充：已接入逐通道 Channel Base Power，Stimulus 页含 Power 字段及独立 Power 页，Pwr Bw Avg 增加 Power 页。真实设置使用 VN10 已确认的 double dBm 合同，并进入对应通道逐点计划；底部 Pwr 和 Bw 分别显示该通道配置。功率范围和 UI 精度属于项目实现，不能据此宣称等同所参考 ZNB 型号。滤波器类型、逐端口功率偏移及平均能力仍未开放。

灰显说明按原机职责区分：[第 557–559 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=557)的 `Restart Manager` 设置 Restart Sweep 作用于活动通道或全部通道，以及每通道扫描次数；`Sweep Controller` 仅开关显示 Idle、Preparing、Sweeping、Calculation 等阶段的 Sweep Info 窗口；Pipelining 控制测量数据流水线。[第 552–553 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=552)的 `Trigger Manager` 用于 Multiple 下各测量序列的触发源、信号条件、延迟及外部信号逻辑组合。不能将这三者解释成按事件重启、外部扫频控制器或触发输出路由。

已选中的可用软键在鼠标悬停时继续保持蓝色；正在执行或只读时统一显示禁用样式，选中状态仍来自服务读回。

## 来源与复核

本项目既有[参考资料索引](../reference/znb8/README.md)以 Getting Started v70 / User Manual v74 为基线。[官方手册入口](https://www.rohde-schwarz.com/nl/manual/rs-znb-user-manual_78701-29151.html?change_c=BE)在核对时标注 **v74、FW V4.25、2026-07-21**。不同固件的图形细节可能不同，本结论不混用 ZNB3000、ZNH 或 ZNL 的菜单。

本地 PDF 的 SHA-256 为 `874901ee4e880dbca9a606406df448494f610233f0cd3b6a61244464b19fdccd`，与[已有来源清单](../reference/znb8/source-manifest.json)一致。页码已经同时核对印刷页与 PDF 页；完整 PDF 继续保留在现有临时参考目录，不重复纳入仓库。
