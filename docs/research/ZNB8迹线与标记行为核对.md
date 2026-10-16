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

2026-10-14 复核：ZNB 手册的 Marker addressing 明确命令作用于 `<Chn>` 通道的活动迹线；`<Chn>` 只选通道，不能由这个前缀推断 Marker 是通道共享对象。[同一手册，第 1029 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=1029) Coupled Markers 联动同编号标记的添加、删除和刺激位置，各迹线的 Y 读数仍由各自测量响应决定。[同一手册，第 97 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=97) 范围可选全部通道、同通道或同图窗，对应 `ALL / CHAN / DIAG`。[同一手册，第 522–523 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=522) 当前 VnaPro 保留独立普通标记，并在 Marker 页明确当前迹线及 Coupled 未实现的边界。

工具栏新增标记放到目标图表的活动迹线上；右上角同时显示 X/Y，持续扫描时 Y 随测量更新。删除可将单个标记或整组读数拖到垃圾桶；最后一条迹线不能删除。[官方 Getting Started v70，第 47–48 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/getting_started/1316_0062_01/ZNB_GettingStarted_en_70.pdf#page=48)

## 2026-10-15：拖到垃圾箱的对象语义

v74 工具栏说明和删除章节区分单击及四种拖放对象；顶部 trace info field / trace line 指迹线标题信息，不是曲线上任意采样点。[官方 v74 第 46 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=46)、[第 56–57 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=56)

| 操作 | 官方语义与本项目映射 |
| --- | --- |
| 单击垃圾箱 | 删除活动迹线。 |
| 把迹线标题拖到垃圾箱 | 删除起拖的指定迹线，不要求先把它变成活动迹线。项目通过命名测量删除事务执行。 |
| 把单个 Marker 符号拖到垃圾箱 | 关闭该迹线的指定编号，保留坐标与属性。项目不在同次松手后另写 Marker 频率。 |
| 把 Marker 信息字段拖到垃圾箱 | 关闭该字段所属迹线的全部 Marker，保留成员属性；不是只关闭活动编号，也不是全仪器 All Off。 |
| 把图窗右上角下拉把手拖到垃圾箱 | 删除该图窗及其中迹线；项目用目录/显示双 CAS 原子事务执行。 |

关闭 Marker 的属性保留依据第 501–502 页；删除迹线则丢失所属 Marker。第 448–449 页规定单迹线图窗随迹线删除、最后迹线不能删除；第 795 页说明删除图窗会删除其中迹线、重排图号，最后图窗禁删。以上是手动界面的保护，不能据此改写所有远程解除显示关联命令的合同。[官方 v74 第 448–449 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=448)、[第 501–502 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=501)、[第 795 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=795)

本项目对拖放冻结名称/Marker 编号/图号及起始版本，松手时拒绝过期令牌、Busy 或失去控制权；不先激活源对象，不因别处切换活动迹线删除错对象。测量和图窗拖放同时检查逻辑与显示快照，Marker 关闭使用显示版本；图窗编号变化由后端重排并读回。标题原来的跨图窗移动、信息字段原来的九宫格/Table 迁移仍保留，只有落在垃圾箱才触发上述动作。

最后一条迹线、最后一个图窗受保护，但最后迹线上的 Marker 仍可单独或整组关闭，不能因为“删除迹线”按钮禁用就连带禁止 Marker 投放。删除最后成员后非末图窗的清理沿用后端既有 Reconcile。`DISP:WIND:TRAC:DEL` 只解除显示槽关联，不是本次垃圾箱的测量删除入口。原机有 Undo，本项目尚未实现，工具栏保持禁用，不宣称能撤销已经删除的测量。[官方 v74 第 56 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=56)

验收分别检查非活动迹线标题、单 Marker、整组信息字段（包括 Table）与图窗把手；还需覆盖旧版本拒绝、最后对象保护、Marker 属性重启恢复，以及一次松手只有删除/关闭或原拖动动作之一。垃圾箱命中距离、悬停样式及删除后的活动对象回退属于项目实现，手册没有给出精确像素或排序算法。

## 标题与右上角读数

- 官方 Trace List 示例跨行采用对齐的列起点；标题包含迹线名、测量量、格式、尺度和参考值，活动标题蓝底高亮。[User Manual v74，第 91 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=91)
- 右上角 Marker Info 的编号及坐标使用所属迹线颜色，活动标记前有圆点。不同迹线各自拥有 M1，因此列表中的重复编号不是全局编号错误。[第 94 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=94)
- Info Table 中的行使用类似 `M1 Trc1` 的归属标识；Info Window 选择器区分 `Marker - Trc1`、`Marker - Trc2`。[第 95、805 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=95)
- `Active Trace Only` 默认关闭，所以普通初始设置下不能只显示活动迹线标记，也不能将所有迹线的标记重新编号为一个全局集合。[第 1210 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=1210)

已核对页面没有规定多迹线 Overlay 的读数必须按 Trc 名称数字排序，也没有规定默认 Marker Info 必须额外显示 Trc 列。本项目保留后端图窗成员顺序和每迹线编号，给多迹线读数增加可见的 Trc 归属列用于消歧；这是参考原机 Info Table 归属表达的显示补充。标题按实际字体完整字宽与可用容器宽度计算等宽列，不固定三列或四列，自然排列，不用固定 84 像素高度截断；窄图窗省略次要字段。

单图窗可叠加大量迹线，不因屏幕只能容纳若干标题行就截断或删除；128 只表示项目总测量和显示资源预算。图窗数量、图窗内槽身份以及循环颜色数与迹线数量分别处理，不把四色调色板或十个普通标记编号当成单图窗迹线容量。

### 颜色数量

System Config 的官方示例为 8 种迹线颜色，颜色数量可配置；默认配色规则按图窗成员顺序循环，删除成员可能使后续成员换色。[第 803、815、1566 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=815) 图窗局部活动高亮及删除后选择的证据边界见[图窗活动迹线与删除行为核对](ZNB8图窗活动迹线与删除行为核对.md)。

本项目由四色扩展为八色循环；每窗重新开始，标题、曲线、标记符号和读数使用同一颜色来源。保留已有绿、黄、青、紫，并补充橙、蓝、红和浅色。已核对资料没有给出完整出厂八色 RGB 表或固定色序，具体 RGB 是项目参考色板，不称为原机出厂色板的逐值还原。

### 滚动与密度边界

手册明确支持表格和对话框滚动，但没有给出 Trace List 或图内 Marker Info 的内嵌滚动条规则。小图窗可省略部分标题字段；密集标记信息可移动到 Info Table。独立 Info Window 的文字自适应不能当作图内 Marker Info 的自动缩放依据。[第 52、92、95、804 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=95)

当前取消标题区人为高度上限、标题和图内 Marker 信息列表的内部滚动条，并提供全局 Info Table 的 Bottom/Left/Right。用户将每条迹线的信息字段移入表格，由独立表格容器承接密集内容；前端不自动移动、丢弃或截断后端迹线。容器显隐、位置、每迹线字段归属、每图窗 Marker Info 开关及全局 Active Trace Only/Stimulus Info/Font Size 均由 C++ 保存。隐藏 Table 保留字段归属，移动不自动开启容器，这是项目的明确规则；标题列数和 Table 像素范围没有已核验的原机算法。详细事实与边界见[信息表与标题排版核对](ZNB8信息表与标题排版核对.md)。固定屏幕面积无法同时完整容纳任意多标题和全部标记，资源预算不等同于同屏可读性保证。

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

### 单 Marker 拖离与整组信息框的区别

第 56 页要求单个 Marker 先纵向拖离迹线，再拖入 Delete；拖 Marker 信息框则关闭该迹线的整组 Marker。用户确认“所有 Marker 消失”来自右上角读数信息框，这与官方整组语义一致，不能据此把 All Off 改为只关活动编号。[官方手册第 56 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=56)

原实现只有指针松手落点判定，预览始终吸在曲线上且被图窗裁剪，没有完成可见的“拖离”。本次补充脱离后的跟手符号及垃圾箱高亮，保持源 traceName、markerId 和显示版本冻结；有效落点仅关闭该编号，不发送频率移动或 All Off。脱离的判定要求至少 3 CSS px 移动，同时具有 12 CSS px 纵向意图及 12 CSS px 离线距离，并扣除抓取偏移；这是项目阈值。静止单击、纯横向越过陡边仍走原来的选择/移频。脱离后的无效落点及取消恢复原状态、不改频率，也是项目取消规则，不声称手册规定了相同阈值与回退算法。

本次补充修正通过前端全量 519 项、类型检查、生产构建及发布包检查。独立 8084 模拟实例中，M1/M2/M3 同时启用，纵向空落点取消；M2 正常水平拖动从 4.005 GHz 移至 4.84395 GHz，再拖入垃圾箱后只留下 M1、M3。后端显示版本从 8 变为 9，M2 关闭但保留原频点及采样索引，其他编号完整设置、采集版本和 runId 保持不变；用户正在测试的 8083 实例不参与这些写入。原机单符号和整组字段语义由手册核实，脱离阈值及极端几何主要由真实组件回归覆盖。

## 复核来源

[R&S 官方手册入口](https://www.rohde-schwarz.com/nl/manual/rs-znb-user-manual_78701-29151.html?change_c=BE)在核对日提供 v74 / FW V4.25，发布日 2026-07-21。本地 PDF 与既有来源清单一致，SHA-256 为 `874901ee4e880dbca9a606406df448494f610233f0cd3b6a61244464b19fdccd`。以上页码均已按该 PDF 正文复核，不混用其他仪器型号。

原生菜单图已从本地官方 PDF 第 501 页提取到临时参考目录 `C:/Users/Administrator/.codex/tmp/vnapro-znb8-reference/um-v74-p501-marker-0.png`，用于实现对照；不将临时路径作为产品运行依赖。
