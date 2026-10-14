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

2026-10-14 对照 [v74 第 556–557 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=556)再次核对：ZNB 的 `Continuous / Single` 是互斥模式，`Start Sweep`（正文称 `Restart Sweep`）是独立动作；`All Channels on Hold` 把所有通道设为 Single 模式，并不立即启动。原图控件纵向排列，右侧 `Sweep Control` 页签分两行显示。

VnaPro 当前仅有 Ch1，保留现有控制契约：`Continuous` 开始连续扫描，`Hold` 停止扫描；`Single sweep` 会立即采集一帧，完成后回到 Hold。前两项按服务端回读状态显示选中，单次扫描作为独立动作显示，不再固定使用与选中状态相同的蓝色。软工具页签允许换行，避免窄面板裁切完整名称。这是现有能力的映射，不是原机 Single 模式与 Restart Sweep 的完整复刻；尚不提供 Sweeps 计数、Restart Manager 和多通道控制。

界面的 `Continuous` 和 `Single sweep` 动作均使用 Immediate 触发源；SCPI 单独设置 Sweep Mode 为 CONT 时会保留现有触发源，MAN/CONT/Running 表示等待 Manual trigger。模式选中只表示已配置的 Sweep Mode，不能据此判断是否正在取数；Trigger 页显示实际触发源，手动触发仅在已布防的 MAN 状态可用。

## 来源与复核

本项目既有[参考资料索引](../reference/znb8/README.md)以 Getting Started v70 / User Manual v74 为基线。[官方手册入口](https://www.rohde-schwarz.com/nl/manual/rs-znb-user-manual_78701-29151.html?change_c=BE)在核对时标注 **v74、FW V4.25、2026-07-21**。不同固件的图形细节可能不同，本结论不混用 ZNB3000、ZNH 或 ZNL 的菜单。

本地 PDF 的 SHA-256 为 `874901ee4e880dbca9a606406df448494f610233f0cd3b6a61244464b19fdccd`，与[已有来源清单](../reference/znb8/source-manifest.json)一致。页码已经同时核对印刷页与 PDF 页；完整 PDF 继续保留在现有临时参考目录，不重复纳入仓库。
