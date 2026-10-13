# ZNB8 信息表与标题排版核对

核对日期：2026-10-13。界面基线为 R&S ZNB User Manual v74 / FW V4.25；[官方手册入口](https://www.rohde-schwarz.com.cn/manual/rs-znb-user-manual_78701-29151.html)本日仍列出该版本，发布日期为 2026-07-21。以下页码按官方 PDF 正文，不混用其他型号。项目的远程命令仍采用已核验的 Keysight 子集，不因界面参考 ZNB 而加入 R&S 命令。

## 原机行为与证据

| 项目 | 官方行为 |
| --- | --- |
| 图窗容量 | 单个 diagram 的迹线数量实际上不受限，可来自不同 channel。项目的 128 项预算不能称为原机单图窗上限。第 89 页。 |
| Trace List | 每条迹线的条目依次包含名称、测量量、格式、尺度/参考值，必要时还有通道、类型等字段；活动条目高亮。第 91 页原图中，上排 Trc1 与下排活动 Trc5 文字长度不同，标题背景右边界相同，测量量保持独立色块；该示例支持高亮铺满本列槽位。第 91–92 页。 |
| 标题空间不足 | 第 92 页明确规定图窗太小时隐藏部分 section，扩大或最大化后恢复完整字段；没有给出文字截成省略号的规则，也没有规定必须三列或四列。第 91 页的例图为两列。 |
| Marker Info Field | 第 94 页描述图窗内全部 Marker 的坐标可显示在 info field，默认位于右上角；第 521 页的 Show 是图窗作用域。普通编号按迹线独立，编号与坐标使用所属迹线颜色，活动行前有圆点。第 94、500–502 页。上述依据未明确多迹线时独立字段的数量及移动单位。 |
| Info Table | 一个全局屏幕容器，可位于底部、左侧或右侧。默认隐藏，通过 `System → Display → Config → Info Table → Show` 显示。第 95、801、1210 页。 |
| 字段移动 | 将 Marker Info Field 拖放到 Info Table，为曲线腾出空间。第 95 页。 |
| Table 外观 | 第 95、801 页重复使用同一张黑底局部示意图：左侧是 Trc1 的 Marker 字段，右侧是 Bandstop 搜索结果字段，两块之间有细边框；不是两条迹线的 Marker 字段实例。左块示例的行顺序为活动圆点、M 编号、Trc 名、刺激值/单位、响应值/单位，整行同迹线色，没有通用列头。 |
| Show / Position | Table 默认关闭、位置默认 Bottom；命令分别控制显隐和位置。第 1210 页。字段迁移后的隐藏、恢复细节未在此处说明。 |
| Marker Info Show | 控制指定图窗的标记信息显示，默认开启；不是启停标记本体。第 521、1221–1222 页。 |
| Active Trace Only | 开启后标记信息只显示全局活动迹线；默认关闭。第 521、1210 页。 |
| Stimulus Info | 只控制 Marker 信息中的频率坐标，默认开启；不控制 Channel footer。第 521、1210 页。 |
| Font Size | 作用于 diagram 的 Trace List、Channel List 和信息字段；相对字号范围 80%～170%，默认 100%。第 801、1221 页。 |
| Info Window | 独立窗口，可选择部分 Marker 或 Bandfilter 结果，文字自动适配窗口尺寸；与 Info Table 不同。已在 Table 的信息字段不进入它的候选列表。第 804–805 页。 |

表中依据为[官方 User Manual v74 第 89–95 页](https://cdn.rohde-schwarz.com.cn/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=89)、[Marker Info Field 第 521 页](https://cdn.rohde-schwarz.com.cn/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=521)、[Display Config 第 800–805 页](https://cdn.rohde-schwarz.com.cn/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=800)、[显示设置缺省值第 1210、1221–1222 页](https://cdn.rohde-schwarz.com.cn/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=1210)。

手册第 52 页支持长表的鼠标滚动与触摸滑动，但这是通用表格操作说明，不能据此认定 Info Table 的密集字段使用滚动条，也不能证明图内 Marker Info 或 Trace List 的溢出方式。[第 52 页](https://cdn.rohde-schwarz.com.cn/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=52)。Info Window 的自动缩字号不能当成图内信息字段的自动缩字号依据。

本次重新逐页核对第 91–92、795、801 页：第 92 页明确使用 “some of the sections are hidden”，随后说明扩大或最大化可显示全部 section。手册没有公布固定像素阈值、固定列数或隐藏顺序；第 801 页还允许调整 Trace List 字号。因此应区分图窗过窄时整字段取舍与为了保持四列而隐藏信息，不能把本机字体测量结果当作 ZNB 固定阈值。

2026-10-13 复核了第 94、95、521、801 页完整上下文。第 95、801 页只有相同局部示意，均没有完整工作屏截图，不能核实 Info Table 的整体占屏比例、分隔线是否可拖、尺寸调整和字段过多时的原机布局。第 94 页以单数 info field 描述图窗内全部 Marker，第 521 页以图窗作用域控制 Show；这支持图窗级信息显示，但原文没有明确规定“一窗只能一个字段”，也不足以证明“每条迹线各有一个可独立迁移字段”。当前按迹线拆块与迁移仍需原机多迹线实例核实。

## 当前共享显示合同

C++ `DisplayWorkspace` 保存唯一权威状态，所有写操作继续经过服务执行器的控制权、有限 FIFO 和显示版本 CAS。显示操作不改采集计划、不重启 Continuous。刷新页面和观察窗口读取同一快照；当前没有服务重启后的磁盘恢复。

| 快照字段 | 归属与缺省值 |
| --- | --- |
| `display.infoTable.visible` | 全局容器显隐，默认 `false`。 |
| `display.infoTable.position` | 全局停靠位置，`bottom / left / right`，默认 `bottom`。 |
| `display.annotations.activeTraceOnly` | 全局过滤，默认 `false`，以 `selectedMeasurement` 为活动迹线。 |
| `display.annotations.stimulusInfoVisible` | 全局 Marker 频率显示开关，默认 `true`，不影响底部通道行。 |
| `display.annotations.fontScale` | 全局百分比字号，默认 `100`，合法范围 `[80, 170]`。 |
| `display.diagrams[].markerInfoVisible` | 图窗所属迹线的信息字段显隐，默认 `true`；放入 Table 后也遵循所属图窗开关。 |
| `display.presentations[name].markerInfoLocation` | 每迹线信息字段位置，`diagram / table`，默认 `diagram`；不与 `markers[].enabled` 混用。 |

Info Table 使用全部已保存图窗的成员构造字段，不因 Maximized 暂时隐藏其他绘图区而遗失字段。隐藏迹线不显示其信息字段；`Active Trace Only` 对 Table 和图内字段使用同一过滤。两者统一是依据信息字段概念作出的实现解释，手册未另列 Table 豁免。

字段读取已下发的 C++ Marker 投影，图内与 Table 共用数据和临时拖动预览；前端不再次计算幅度、相位、插值或阻抗。移动整组字段不会移动曲线上的 Marker 坐标，选中一行仍提交所属迹线及明确 `markerId`。

## 明确的项目规则与边界

以下细节没有原机算法或明确行为的证据，按项目合同记录：

- 每条迹线独立构造 Marker 字段块，并可将整块单独迁移到 Table；这是当前实现解释，已核对的单迹线 Marker + Bandstop 示意不能证明多迹线时的原机拆块与迁移规则。
- Show/Hide Table 仅改变容器显隐，不自动修改每迹线的字段位置；隐藏后保留已迁移归属，重新显示后恢复原字段。
- 移动字段不自动开启 Table。页面先开启 Table，再拖放或使用 `Show Info in Table`；返回图内可拖回图窗或选择 `Show Info in Diagram`，也可在 Display 设置选择 `In Diagram`。
- `markerInfoVisible` 统一控制图窗所属字段，不因字段进入 Table 而绕过开关；Overlay/Split 调整图窗后的保留规则由后端执行。
- 标题列数根据实际字体、完整标题字宽与可用容器宽度计算，优先完整显示字段，等宽列自然排列。标题变长时先减少列数，不因追求四列而隐藏信息。只有整个标题区无法容纳单个完整标题时，才依次整项隐藏 Ref、Scale Div、RAW 类型、格式，并按剩余实际字宽排列；同图窗统一字段集合以对齐。放宽后从完整字段重新计算，不截断数值或使用省略号，完整提示与仪器值始终保留。字体、名称和实际数字不做布局归一化。这是项目布局策略，精确阈值与隐藏顺序没有原机算法依据，不固定所有尺寸为四列，不删除迹线或改变后端归属。
- Table 底部区域当前 CSS 最大高度为工作区的 35%；左右区域以 300 px 为布局基准、最大宽度 40%，溢出内容在 Table 中滚动。这些是显示终端的像素布局边界，不是仪器配置或已核实的原机比例。
- 图内字段不设置内部滚动条，也不因条目变多自动转入 Table。密集显示由用户选择字段迁移、字号、Split 或 Maximized；固定屏幕面积不能保证任意数量字段同时完整可读。

本期实现普通 Marker 信息字段、容器位置及相关显示设置。独立 Info Window、Bandfilter 信息字段、Reference/Delta Marker、独立 Marker Format 和用户自定义配色均不属于此次范围，不宣称整套 ZNB 功能 1:1。

操作和验收步骤见[Info Table 操作验收](../InfoTable操作验收.md)。
