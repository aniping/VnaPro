# ZNB8 图窗点击交互核对

核对日期：2026-10-13。以 R&S ZNB User Manual v74 / FW V4.25 为界面基线，补充官方 Keysight E5080B 的交互说明。这里只核实点击、激活与移动，不能将不同厂商行为合称为 ZNB 的精确实现。

## 结论

普通单击图窗不应被统一解释为移动或新增 Marker。ZNB 明确区分激活图窗、激活迹线、激活 Marker 和移动 Marker。用户对“单击 diagram 就改变 Marker”的质疑有官方依据；“点击曲线选中迹线”有 Keysight 的直接依据，但当前 ZNB v74 没有明确规定曲线命中规则。

## ZNB 官方规则

下表页码对应手册印刷页码，也与当前 PDF 页序一致。

| 手势与目标 | 官方行为 | 依据 |
| --- | --- | --- |
| 普通点击图窗中的一点 | 激活该图窗，同时恢复该图窗上次活动的迹线。没有将普通单击定义为 Marker 定位。 | §5.16.1，第 794 页。 |
| 点击上方 Trace List 的对应标题 | 激活该迹线及其图窗。 | §5.16.1，第 794 页；活动状态说明见 §4.1.3.3，第 78 页。 |
| 点击 Marker 符号 | 激活该 Marker。不是将其移动到任意点击坐标。 | §4.2.1.3，第 93 页。 |
| 点击 Marker Info Field 的对应读数行 | 激活该 Marker。 | §4.2.1.3，第 93 页。 |
| 拖动 Marker 符号 | 沿迹线移动 Marker；此直接拖动方法注明只适用于 Cartesian 图。 | 第 93 页；拖放操作表见第 57 页。 |
| 拖动 Marker Info Field | 移动读数区域位置，不能当作移动该区域内所有 Marker 的刺激坐标。 | 第 57、94 页。 |
| 从工具栏拖入新增 Marker 图标 | 在目标图窗活动迹线的投放位置新增 Marker；多迹线时先激活目标迹线。 | 第 55–56 页。 |
| 点击 Marker 硬键或编号按钮 | 首次 Marker 硬键创建 M1；编号按钮启用对应 Marker，普通 Marker 默认位于扫描中心。 | 第 500、502 页。 |

以上操作依据：[官方 User Manual v74，第 794 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=794)、[第 93 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=93)、[第 55–57 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=55)、[第 500–502 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=500)。

官方第 64 页也明确分开触摸或点击时的激活与拖放时的移动。单击激活不应调用 Marker 刺激位置写入，也不应自动启用已关闭的 Marker。[第 64 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=64)

## 曲线点击的证据边界

ZNB v74 第 78 页称在 diagram area 中选中迹线后它成为活动迹线，第 794 页给出明确的 Trace List 点击方法；已核实章节没有给出点击绘制曲线的距离阈值、重叠曲线优先级或循环选择策略。因此，不能把项目自行制定的命中算法称为 ZNB 原机算法。

Keysight 官方 E5080B 的 Screen Tour 明确说明：左键单击曲线或 Trace Status 都会选中迹线。Marker 则通过拖动其符号改变位置，Smith/Polar 和 Fixed Marker 不允许这种拖动。用户所描述的“点击线选迹线”确实是有一手资料支持的商用交互，而不是仅凭使用习惯猜测。[Keysight Screen Tour](https://helpfiles.keysight.com/csg/e5080b/Front_Panel/XScreen.htm)、[Keysight Markers](https://helpfiles.keysight.com/csg/e5080b/S4_Collect/Markers.htm)

## 模式例外与本轮范围

- ZNB 的 Zoom Select 开启后，图内拖动用于选择放大矩形；Graphical Zoom 只适用于 Cartesian。不能继续将这种拖动当作 Marker 移动。Stimulus Zoom 是另一模式，会改变扫描区域和纵向缩放。[第 51、61–62 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=61)
- Marker Search 可把活动 Marker 移至搜索结果；Tracking 可在每次扫描后重新搜索。这属于明确调用的搜索功能，不是普通图窗单击。[第 97 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=97)
- ZNB 双击说明有层次区别：第 50 页将它简述为最大化图窗或恢复原尺寸；第 65 页更具体地说，双击会最大化图窗并隐藏其非活动迹线。当前只修复普通单击与 Marker 拖动，不能把现有最大化按钮声称为该复合手势的完整实现。[第 50 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=50)、[第 65 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=65)
- Arbitrary Marker 可以设置响应位置，Coupling 可以联动其他迹线，参考与差值 Marker 有各自属性。这些能力不能由统一的“画布点击改 Marker”代替，也不应在本轮点击修复中假装实现。[第 501–504、522 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=501)

## 本项目执行建议

下面是依据上述事实制定的项目交互规则，命中阈值与优先级是实现选择。

本轮代码核对发现 `DiagramPane.vue` 的 `clickPlot` 在图窗已经活动时直接调用 `placeMarker`，把背景单击连接到了 Marker 定位；在未活动时则先调用 `selectPane`。因此同一位置的单击随图窗活动状态改变含义，容易先选中、再误移动。应按目标明确拆分：

| 普通操作 | 提交到后端的语义 |
| --- | --- |
| 单击空白区 | 激活图窗，恢复该图窗上次活动迹线。 |
| 单击曲线或迹线标题 | 激活所属迹线及图窗。 |
| 单击 Marker 符号或读数行 | 激活所属迹线和对应 Marker，不改变位置。 |
| 从 Marker 符号开始真实拖动 | 预览移动，抬起时提交 Marker 位置。 |

1. 默认单击空白区只激活图窗及其上次活动迹线。不能新增、启用或移动 Marker。即使图号已经活动，若 SCPI 等途径选中了不显示的测量定义，空白点击仍请求后端恢复该图窗的上次活动迹线。
2. 单击已显示曲线或上方标题激活所属迹线，由 C++ 保存活动状态。曲线点击采用 Keysight 已明确提供的商用交互补充，不宣称已经证明 ZNB 的精确曲线命中细节。
3. 单击 Marker 符号或读数行激活其所属迹线和对应 Marker，保持刺激位置不变。Marker 符号应先于背后的曲线命中；隐藏迹线不参与曲线拾取。
4. 只有从已显示 Marker 符号开始的真实拖动才定位 Marker；按下后未跨过拖动阈值的抬起仍然是激活，不发送位置写入。拖动预览可以使用前端当前显示帧，抬起后通过现有接口提交 C++；取消、冲突或拒绝后返回后端实际状态。
5. 曲线拾取计算指针到屏幕线段的距离，而不是只看同一 X 位置的数据值。项目采用 6 CSS px 容差，先选最近曲线，同距时活动曲线优先，其余按后绘制顺序优先；仍可用标题精确选择被遮挡迹线。绘制与拾取共用同一组后端投影，无效点断开线段，裁剪与矩形、圆图渲染器一致。这是项目的确定性像素策略，未声称为原机算法。
6. 普通图窗单击不论格式均不改变 Marker 位置；Smith/Polar 也不能把背景 X 坐标当作频率。本轮保留已有不同格式的 Marker 专用拖动能力，不借点击修复扩展或删改手势格式范围。官方第 93 页只明确 Cartesian 拖动，项目已有的圆图拖动仍需作为单独差异核实。
7. 当前图窗、迹线和 Marker 的已应用选择及位置仍属于 C++ 显示域。前端只执行渲染命中检测与临时手势预览，观察窗口和刷新必须看到相同已应用状态。

验收至少覆盖：空白点不改位置且不启用 Marker、不同曲线选择、项目确定的重叠命中策略、Marker 单击无位置写入、真实拖动一次提交、Smith/Polar 普通背景单击不误改频率、跨图窗恢复上次活动迹线、观察窗口与刷新一致。双击最大化与隐藏其他迹线的复合动作不在本轮范围。

## 本轮验证

2026-10-13：真实 SFC 回归先捕获四个失败：活动图窗背景误移动 Marker、点非活动曲线仍移动当前 Marker、非活动图窗点线只激活图窗、Marker 全关闭时点线不选择。最终检查还捕获“选中隐藏测量定义后空白点击未恢复图窗活动迹线”的失败。修复后 WebUI 完整测试 187/187、类型检查和生产构建通过，包括九项组件事件回归与十八项几何边界验证。

实际浏览器在 Continuous、3980 点场景中，空白点击保持显示版本 201 和 Trc17/M1 频率 4.353395325 GHz。临时将 Trc1 显示为 Real 并设置 −5…5 刻度以分离曲线后，单击曲线使活动迹线从 Trc17 变为 Trc1，所有 Marker 设置保持；只读观察窗口显示同一选择。单击 Trc17/M2 只切换选中成员，全部 Marker 坐标保持。随后恢复 Trc1 原格式、刻度和 Trc17/M1 选择，并验证刷新读回；运行编号 4 与采集版本 3 保持一致。真实鼠标拖动的松手单次提交由本轮 SFC 事件回归验证，不用该组件测试冒称本轮鼠标拖动实测。

## 来源版本与核验方式

2026-10-13 在线核对的[R&S 官方手册入口](https://www.rohde-schwarz.com.cn/manual/rs-znb-user-manual_78701-29151.html)列出 v74 / FW V4.25，发布日为 2026-07-21。正文依据从已有完整官方 PDF 逐页读取；Getting Started v70 第 48、57–58 页也区分目标迹线激活、Marker 激活与拖放。网络检索出现的旧手册第三方镜像没有作为上述 ZNB 事实依据。
