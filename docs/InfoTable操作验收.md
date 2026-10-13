# Info Table 操作验收

本功能参考 ZNB8 的全局 Info Table 容器及 Marker 信息字段，具体依据和项目边界见[官方行为核对](research/ZNB8信息表与标题排版核对.md)。所有已应用显示设置保存在正在运行的 C++ 服务；页面只显示快照、提交操作和维护临时输入/手势预览。当前不包含服务重启后的磁盘恢复。

常规读数使用图窗内的浮动 Marker Info Field，Info Table 是默认隐藏的辅助停靠容器。前次验收页面开启 Table 仅用于演示，不代表原机默认主界面。当前按迹线独立拆块是实现解释，仍需原机多迹线实例核实；第 95、801 页的同一示意图显示 Marker 与 Bandstop 两种字段，并非两条迹线的 Marker 块。

## 使用方法

1. 在持有控制权的页面打开 `Display` 软工具。`Show Table` 默认 Off，开启后选择 Bottom、Left 或 Right。
2. 启用目标迹线的 Marker。将其图内信息字段拖到 Table；也可在该字段右键选择 `Show Info in Table`，或在 Display 中选择活动迹线的 `In Info Table`。操作迁移整条迹线的信息字段，不改变 Marker 坐标。
3. Table 按字段分块，每行显示活动圆点、M 编号、Trc 名、频率及响应值，整行跟随迹线颜色；编号继续按迹线独立使用 M1～M10。点击某行选择对应迹线和 Marker。
4. 将字段拖回所属图窗，或右键选择 `Show Info in Diagram`；Display 中的 `In Diagram` 也可恢复图内归属。
5. `Marker Info` 控制当前图窗所属迹线的信息字段，包括已放入 Table 的字段；它不关闭 Marker 本体。`Active Trace Only` 只显示全局活动迹线的信息，默认 Off。
6. `Marker Stimulus Info` 只隐藏/显示 Marker 的频率，底部通道行不变。`Font Size` 输入 80%～170% 后 Apply，标题、通道列表和信息字段随保存字号显示。

关闭 Table 会隐藏容器并保留字段归属，不自动把字段移回图内；重新开启后恢复。页面只有 Table 已开启时允许向其迁移字段，移动不会自动开启容器。该显隐与归属独立是明确的项目规则，手册没有规定相反的自动行为。

Table 使用全部已保存图窗，Maximized 只隐藏其他绘图区，不删除其字段。底部 Table 最大高度为工作区的 35%，左右布局基准为 300 px、最大宽度 40%；内容过多时只在独立 Table 中滚动。这些 CSS 边界是项目像素布局，不是原机配置。标题根据实际文字与可用宽度排列，不固定三列或四列；窄窗省略次要字段，不删除迹线。

## HTTP 合同

以下请求均为 `POST /api/v1/display` 的动作字段；实际请求还须带有效控制会话、`requestId` 和 `expectedDisplayRevision`，沿用 202 操作等待及完成读回，不乐观覆盖快照。

| 动作 | 参数与结果 |
| --- | --- |
| `infoTable` | `enabled`、`position` 至少一个；位置为 `bottom / left / right`，修改全局容器。 |
| `displayAnnotations` | `activeTraceOnly`、`stimulusInfoVisible`、`fontScale` 至少一个，修改全局信息显示。 |
| `markerInfo` | `enabled`，可指定 `diagram`；省略图号则针对执行时活动图窗。 |
| `markerInfoLocation` | `traceName` 与 `location:"diagram" / "table"`，改变该迹线的信息字段归属。 |

`GET /api/v1/status` 的 `display.infoTable`、`annotations`、`diagrams[].markerInfoVisible` 和 `presentations[name].markerInfoLocation` 为权威读回。无变化操作不增加显示版本；无权、旧 CAS、非法位置、错误作用域或越界字号拒绝时不得部分修改状态。既有 `marker` 动作的 `markerAction:"select"` 在一次候选事务内选择 Marker 并激活所属迹线、图窗；任一步失败整体丢弃。既有 Keysight SCPI Marker 查询继续读取相同标记坐标；本次不添加未经核实的显示命令。

## 验证步骤

- 默认 Table 隐藏、Bottom，字体 100%，全部迹线 Marker 信息可见，频率可见；不因新页面打开而重置已应用设置。
- 创建至少两条不同颜色迹线，各启用重复编号 M1/M2。迁移第二条字段，确认第一条仍在图内，Table 显示正确 Trc 归属和同色读数。
- 从非第一条迹线的字段开始拖放，确认请求目标是实际字段的迹线；返回图内、点击 Table 行及右键移动均经过后端。
- 在 Continuous 中完成迁移、显隐与位置修改；`runId`、采集版本和 Marker 坐标不因这些纯显示操作改变，帧序号继续推进。
- 关闭再开启 Table，迁移归属保持；另一只读观察窗口与刷新得到同一位置、显隐、字号和活动标记，观察窗口不能写。
- 分别验证 Bottom、Left、Right，80% 和 170%；密集字段可在 Table 滚动，图内无信息列表内部滚动条。调整窗口宽度时标题可自然变为两、三、四列，条目完整且对齐。
- Active Trace Only 开启后只显示全局活动迹线字段；改变活动迹线、跨图窗选择、Maximized 时均不误用每窗主迹线。
- Marker Info 关闭只隐藏所属图窗信息字段，恢复后属性不丢；Marker Stimulus Info 关闭只移除频率，Channel footer 与响应值保持。
- 验证 Overlay/Split、最大化、删除图窗/迹线和 Clone 后的字段状态，删除名称不残留，最大化不丢其他图窗的 Table 字段。
- 改动格式并拖动 Marker，Table 与图内共享后端投影，拖动期间读数与三角形同步；松手一次提交，失败回到权威状态。
- 验证旧版本 CAS、只读控制权、空动作、错误图号、未知位置、越界或非有限字号均拒绝且状态不变。

## 本轮结果

2026-10-13 验收结果：

- 正式 C++ 完整构建、CTest **48/48** 通过，无失败或重试；CLion 目录已同步最新全量构建，受影响的领域、共享 HTTP 显示及 SCPI 图窗测试 **5/5** 通过。
- WebUI 完整测试 **160/160**、类型检查与生产构建通过。真实 Vue 调度回归验证切换 Smith 主坐标后重测标题，普通 Continuous 帧不重测。
- 在现有 3980 点、19 个定义、17 条显示迹线的 Continuous 场景核对 Bottom、Left、Right、80%/170% 字号、显隐与字段归属独立、仅活动迹线、频率文本开关及双向右键迁移。底部通道信息不受频率文本开关影响。
- 806 px 标题容器在 100% 字号下实际为四列；侧边表格缩窄绘图区后自然为两列。标题不设置内部滚动条。
- 临时显示已有 Trc6 后，三个字段共 18 个 Marker 在 170% 字号下表格可视高度 253 px、内容高度 351 px，只在独立 Table 内滚动；图中 18 个 Marker 本体仍全部存在。完成后还原 Trc6 原来的显示与字段位置，未改 Marker 坐标或编号。
- 图内与 Table 的字段拖放、跨迹线字段归属和 Marker 手势预览通过 SFC 行为回归；实际浏览器验证了 Display 入口及双向右键迁移。未将自动组件测试冒称真实鼠标拖动验收。
- 刷新与只读观察窗口保留相同停靠、字段和字号。表格点击 Trc17/M5 后，活动迹线与活动标记同时改变，观察窗口同步且不能写；该导航缺陷已在 C++ 候选事务中修复，跨图窗与非法编号回归验证整体提交或整体拒绝。
- 最终页面保留底部 Table，Trc17/Trc1 两个字段共 14 条可见读数；原 17 条显示迹线、Marker 配置、刻度及活动迹线 Trc13 已核对恢复。全部 18 个已启用 Marker 中，另外四个仍属于未显示的 Trc6。
- 最终 Continuous 采集帧序号实测 `175 → 238`，`runId=4`、采集版本 `3` 保持一致。

以上列出本轮真实执行的验证；其余验证步骤作为后续回归清单。尚未实现独立 Info Window、Bandfilter、Reference/Delta、独立 Marker Format，不以本功能声称完整原机兼容。

随后复核原机依据时，已将演示页面的 Trc1、Trc17、Trc13 信息归属迁回 Diagram，并关闭 Table；后端读回确认，14 条可见浮动读数与 14 个图中 Marker 保留，活动迹线仍为 Trc13，Continuous 和 3980 点配置保持。上面的底部 Table 状态为前次功能演示的历史验收结果。
