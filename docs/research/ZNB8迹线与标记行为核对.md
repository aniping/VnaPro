# ZNB8 迹线与标记行为核对

核对日期：2026-10-12。界面基线为 R&S ZNB User Manual v74 / FW V4.25，补充 Getting Started v70；SCPI 保持本项目的 Keysight 命令语法。

2026-10-13 补充核对：单图窗容量、标题排版和多迹线标记读数。

## 迹线数量与编号

- ZNB 的迹线数量不等于物理端口数量。官方产品资料给出 `Number of traces > 100`，同时列出 2 / 4 端口型号。因此将界面固定为四条迹线没有依据；不过 `> 100` 也不是“最大值为 100”。[ZNB 官方产品资料，第 34–35 页](https://cdn.rohde-schwarz.com.cn/pws/dl_downloads/pdm/cl_brochures_and_datasheets/product_brochure/3608_3278_12/ZNB_bro_en_3608-3278-12_v0500.pdf#page=34)
- User Manual §4.1.3 / §4.2.1 第 77、89 页称同一图表可容纳实际上不受限的迹线，并可来自不同通道；未给出固定的每通道或每图表硬上限。应保留资源保护，但不能将项目容量上限称为原机上限。[官方 User Manual v74](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=77)
- `Trace Config → Traces → Add Trace` 复制活动迹线的设置，放在同一图表并激活新迹线；两条曲线可能重合。默认名 `Trc<n>` 中，n 是当前最小未占用正整数，删除后可以复用。`Add Tr+Diag` 创建新图表。[同一手册，第 448 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=448)
- 用户提到最后一条 `TrcX` 持续累加，是描述新增操作反复替换第四个显示槽的故障，不是要求永不复用编号。项目按 ZNB 的最小空闲正整数分配名称，删除后允许复用；迹线身份由后端分配，不能由颜色数组索引决定。

## 多标记交互

| 项目 | 官方行为与页码 |
| --- | --- |
| 容量 | 每条迹线有 M1～M10，另有参考 R；不同迹线独立。v74 第 500–502 页。 |
| 菜单 | `Trace → Marker → Markers`；按 Marker 硬键首次自动启用 M1。第 500 页。 |
| 新增 | `Add Marker` 采用下一空闲编号；编号按钮直接启用对应标记，初始普通标记位于扫描中心。第 502 页。 |
| 活动标记 | 点击曲线上符号或读数行可以激活。第 93 页。 |
| 关闭 | `On` 关闭当前标记，`All Off` 关闭活动迹线全部标记；关闭保留位置和属性，删除迹线才丢弃。第 501–502 页。 |
| 参考与差值 | R 是差值参考；普通 M 可切换 ΔM，进入 Delta Mode 同时启用 R。第 93、502 页。 |

表中行为依据[官方 User Manual v74 第 500–502 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=500)，活动标记与类型定义见[第 93 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=93)。原文必要短句为 “Uses the next ‘free’ marker number”。

官方图中的 `Markers` 页依次放置 Stimulus、Marker On、All Off、Delta Mode；随后是双列 Mkr 1～10，末行 Ref Mkr / Add Marker，底部 Coupled Markers。尚未实现的能力应禁用或隐藏，不能假装可操作。[同一手册，第 501 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=501)

工具栏新增标记放到目标图表的活动迹线上；右上角同时显示 X/Y，持续扫描时 Y 随测量更新。删除可将单个标记或整组读数拖到垃圾桶；最后一条迹线不能删除。[官方 Getting Started v70，第 47–48 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/getting_started/1316_0062_01/ZNB_GettingStarted_en_70.pdf#page=48)

## 标题与右上角读数

- 官方 Trace List 示例跨行采用对齐的列起点；标题包含迹线名、测量量、格式、尺度和参考值，活动标题蓝底高亮。[User Manual v74，第 91 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=91)
- 右上角 Marker Info 的编号及坐标使用所属迹线颜色，活动标记前有圆点。不同迹线各自拥有 M1，因此列表中的重复编号不是全局编号错误。[第 94 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=94)
- Info Table 中的行使用类似 `M1 Trc1` 的归属标识；Info Window 选择器区分 `Marker - Trc1`、`Marker - Trc2`。[第 95、805 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=95)
- `Active Trace Only` 默认关闭，所以普通初始设置下不能只显示活动迹线标记，也不能将所有迹线的标记重新编号为一个全局集合。[第 1210 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=1210)

已核对页面没有规定多迹线 Overlay 的读数必须按 Trc 名称数字排序，也没有规定默认 Marker Info 必须额外显示 Trc 列。本项目保留后端图窗成员顺序和每迹线编号，给多迹线读数增加可见的 Trc 归属列用于消歧；这是参考原机 Info Table 归属表达的显示补充。标题采用容器宽度决定的等宽列，自然排列，不再用固定 84 像素高度截断。

单图窗可叠加大量迹线，不因屏幕只能容纳若干标题行就截断或删除；128 只表示项目总测量和显示资源预算。图窗数量、图窗内槽身份以及循环颜色数与迹线数量分别处理，不把四色调色板或十个普通标记编号当成单图窗迹线容量。

### 颜色数量

System Config 的官方示例为 8 种迹线颜色，颜色数量可配置；默认配色规则按图窗成员顺序循环，删除成员可能使后续成员换色。[第 803、813、1566 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=813)

本项目由四色扩展为八色循环；每窗重新开始，标题、曲线、标记符号和读数使用同一颜色来源。保留已有绿、黄、青、紫，并补充橙、蓝、红和浅色。已核对资料没有给出完整出厂八色 RGB 表或固定色序，具体 RGB 是项目参考色板，不称为原机出厂色板的逐值还原。

### 滚动与密度边界

手册明确支持表格和对话框滚动，但没有给出 Trace List 或图内 Marker Info 的内嵌滚动条规则。小图窗可省略部分标题字段；密集标记信息可移动到 Info Table。独立 Info Window 的文字自适应不能当作图内 Marker Info 的自动缩放依据。[第 52、92、95、804 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=95)

本轮取消标题区的人为高度上限及其滚动条。Marker 浮层的高度约束仍是项目保护策略，当前尚无独立 Info Table；不能因此宣称密集标记布局与原机一致。超密度标题可用已实现的 Split All 或最大化图窗管理，不能由前端自动移动、丢弃或截断后端迹线。固定屏幕面积无法同时完整容纳任意多标题和全部标记，资源预算不等同于同屏可读性保证。

## Keysight SCPI 范围

Keysight PNA / E5080 的普通标记数量是每迹线 15 个，再加参考标记；不能与 ZNB 的十个编号混为同一个容量承诺。[Keysight Markers](https://helpfiles.keysight.com/csg/e5080b/S4_Collect/Markers.htm)

现有命令应扩展编号参数，仍使用正式语法：

```text
CALCulate1:MARKer2:STATe ON
CALCulate1:MARKer2:X 1GHz
CALCulate1:MARKer2:X?
CALCulate1:MARKer2:Y?
CALCulate1:MARKer2:DISCrete ON
CALCulate1:MARKer:AOFF
```

`STATe`、`X`、`Y?` 的官方参数表允许普通编号 1～15，缺省 1；旧版 `DISCrete` 参数表写的是 1～10。上述命令作用于已选择的测量，需先选正确迹线。本次采用 ZNB 的 1～10 是支持的语法子集，不代表完整 Keysight 容量兼容。[Keysight Calculate:Marker 官方命令参考](https://helpfiles.keysight.com/csg/e5080a/programming/gp-ib_command_finder/calculate/marker.htm)

ENA E5063A 另有 `CALCulate1:MARKer<n>:ACTivate`，其普通编号 1～9，10 表示参考标记。该型号规则不能直接套到 PNA 的普通 M10；未明确兼容型号时不新增歧义命令。[Keysight E5063A ACTivate](https://helpfiles.keysight.com/csg/e5063a/programming/command_reference/calculate/calc_mark_act.htm)

## 本项目采用的范围

测量目录、显示集合和显式图窗均最多 128 项，这是项目的内存与响应预算，不是 ZNB 最大值。Trace Config 的 Add Trace 复制活动定义及显示设置，追加到集合并激活，不替换第四条；Diagram 菜单的 Add 采用默认迹线。Split All 为每条已显示迹线创建独立图窗，Overlay All 允许不同格式共享一窗，切换格式不自动分组。当前更完整的图窗操作依据见[通道、图窗与格式行为核对](ZNB8通道图窗与格式行为核对.md)。

每迹线保存十个普通标记及活动编号；新增、选择、关闭和读数共用 C++ 后端。旧 `marker` 字段从集合中的活动标记派生，不保存第二份状态。HTTP 绘图总预算为 8004 个迹线采样值，单条最多 2001 点；格式、Autoscale 和标记仍使用完整原始帧。

本轮实现普通标记的编号选择、独立设置及 All Off。Reference、Delta、Coupled 尚未实现，相关入口不开放；不宣称整套标记能力与 ZNB 1:1。

## 复核来源

[R&S 官方手册入口](https://www.rohde-schwarz.com/nl/manual/rs-znb-user-manual_78701-29151.html?change_c=BE)在核对日提供 v74 / FW V4.25，发布日 2026-07-21。本地 PDF 与既有来源清单一致，SHA-256 为 `874901ee4e880dbca9a606406df448494f610233f0cd3b6a61244464b19fdccd`。以上页码均已按该 PDF 正文复核，不混用其他仪器型号。

原生菜单图已从本地官方 PDF 第 501 页提取到临时参考目录 `C:/Users/Administrator/.codex/tmp/vnapro-znb8-reference/um-v74-p501-marker-0.png`，用于实现对照；不将临时路径作为产品运行依赖。
