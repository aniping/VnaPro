# ZNB8 通道、图窗与格式行为核对

核对日期：2026-10-12。依据为[官方 User Manual v74 / FW V4.25 入口](https://www.rohde-schwarz.com/nl/manual/rs-znb-user-manual_78701-29151.html?change_c=BE)，逐页核对既有官方 PDF。下文页码均为 v74 的 PDF / 正文页；不套用 ZNB3000 或其他厂商规则。

## 默认状态与连续扫描

| 事项 | 官方规则及依据 |
| --- | --- |
| 默认扫描 | Continuous 为默认，`INITiate<Ch>:CONTinuous` 的复位值是 ON。[第 556、1245 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=556) |
| 工厂预设 | S21、dB Mag，默认扫描范围覆盖仪器支持频段。[第 68 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=68) |
| 启动 | 正常关闭后恢复上一会话；启用 Start in Preset 才按预设启动。预设也可为用户文件，因此“每次启动必然连续扫工厂设置”不成立。[第 26、810–811 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=810) |
| 修改通道 | 修改通道设置后准备新扫描；初始化期间仍可改设置，必要时取消旧初始化并重新准备，首个新扫描有状态标识。没有先 HOLD 的交互前置条件。[第 78–79 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=79) |

频率、点数、IF 带宽分别属于通道的 Stimulus、Sweep Params、Bandwidth 设置。[第 523–524、533–534、530–531 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=523)

手册没有在上述条款定义这些写操作打断正在执行的底层调用的精确时点。项目可采用“完整帧边界应用候选、重新准备并继续 CONT”的驱动合同，但应明确这是项目实现，不能将其内部时序说成已核验的厂商时序。

## 多通道与刺激缩放

通道拥有硬件采集设置，迹线只属于一个通道；图窗可显示多个通道的迹线，两者不对应。活动迹线决定活动通道；同一通道在不同图窗的底部信息行应同时高亮，而不是仅点亮当前图窗。[第 77–78、99 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=78)

Channel Config → Channels 的 New Channel 使用默认设置并新建一条迹线；Copy Channel 复制当前通道的全部设置（含已有校准）和全部迹线。二者的 + Diagram 变体把新迹线放在新图，否则放在活动图。Delete Channel 删除通道及其迹线，GUI 保护最后通道。通道号取当前最大值加一，不能把 Trace Config 的 Add Trace 当成创建新通道。[第 668–670 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=669)

用户提供的 `PixPin_2026-10-15_22-15-30.mp4` 中，上图 Ch1 保持原频段，下图 Ch2 执行 Stimulus Zoom 后频段收窄。它与官方“修改活动通道刺激范围”的定义一致；不是一帧数据显示成两个通道，也不是仅显示裁切。[第 445–446 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=445)

Continuous 通道循环扫描；但手册不定义本项目 DLL 的具体调度和取消时点。本项目采用单设备、单 worker、完整帧边界轮转，逐通道保存真实计划、运行身份和不可变帧。16 个同时存活通道、128 条全局迹线以及按通道保存的 16 层 Zoom 恢复历史是项目预算，不是 ZNB 最大值。功率字段使用已确认的 VN10 double dBm 合同（−120…100），逐通道写入真实计划，不照抄视频中某台 ZNB 的可调功率范围。

Power 的远程命令沿项目既有 Keysight 方言，支持 `SOURce<Ch>:POWer[:LEVel][:IMMediate][:AMPLitude]` 命令与查询、单位 dBm；依据为 [Keysight 官方命令参考](https://helpfiles.keysight.com/csg/e5063a/programming/command_reference/source/sour_pow.htm)。该命令形状与本项目驱动可接受的数值范围分别验证，不能把 VN10 的范围写成 ENA 或 ZNB 的额定输出范围。

用户补充的标题及四图截图用于对照字段留白、同通道高亮和 Pwr 信息；截图分辨率与浏览器缩放不同，不能据此宣称像素级间距已等同原机。项目标题改为内容宽度排列、2 px 列间距，并按当前刻度分辨率格式化 Ref，避免将自动缩放的浮点尾数直接当作设置读数展示；这些具体数值属于项目显示规则。

## Format 与 Smith

普通 Format 菜单共十三项：

```text
dB Mag      Phase
Smith       Polar
SWR         Unwr Phase
Lin Mag     Log Mag
Real        Imag
Inv Smith   Delay
Delay Derivation
```

菜单原图及各项说明见[第 435–440 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=435)。`Log Mag` 是幅值的对数纵轴，区别于 `dB Mag`；Delay 使用频率扫描的群延迟计算及 Aperture Points，不能直接等同任意相邻点差分。

数学实现需区分以下定义：

- `dB Mag = 20 log10 |z|`；Log Mag 的响应仍是 `|z|`，只是纵轴采用 log10 刻度，不可将它也转换成 dB。[第 105、438 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=438)
- `τg = -dφ/dω`、`Delay Derivation = dτg/dω = -d²φ/dω²`，`ω = 2πf`。按公式量纲，后者是 `s²`（角度用弧度，rad 视为无量纲），这是单位推导；已核对页没有单列其显示单位。因此若先对 f 求导，必须再除以 `2π`，不能将 `dτg/df` 直接当相同值。[第 95、106 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=106)
- Inv Smith 仍将复数响应的实部/虚部映射到水平/垂直位置；变化是叠加的导纳网格，不能把迹线复数直接取负。对反射 Γ，归一化导纳 `y = (1-Γ)/(1+Γ)`；实际导纳 `Y = Y0·y = G+jB`。中心为匹配，左端短路/无限导纳，右端开路/零导纳。[第 106、109–111、96 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=110)
- SWR 公式为 `(1+|z|)/(1-|z|)`。已核对的定义与 Format 页没有给出 `|z|≥1` 的显示裁切或替代数值规则：等于 1 的分母为零，大于 1 的公式结果为负，不能擅自对分母取绝对值或将复数幅度夹到 1。界面如何呈现特殊结果须明确为项目合同。[第 95、105、437 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=437)

- 格式是逐迹线设置，允许格式和测量量自由组合。[第 77、104 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=104)
- Smith / Inv Smith / SWR 可用于传输或波量，不能以 S11/S22 白名单限制商业界面；但此时不再具有真实反射系数对应的阻抗、导纳或驻波比含义。[第 112 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=112)
- 切格式激活对应默认刻度；改测量量会选该量的默认格式。它们都不能被推定为“自动保存并恢复各旧格式刻度”。[第 441、112 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=441)
- Overlay All 允许同图混合 Cartesian 与复平面；第 796 页原图同时绘制矩形与 Smith 背景，轴刻度按活动迹线。此前将其解释为只画活动格式网格有误；格式变化不应自动拆成两个图窗。[第 796 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=796)，补充证据见[混合网格与整组默认格式核对](ZNB8混合网格与整组默认格式核对.md)。

Polar 的曲线坐标仍是实部 / 虚部，但官方 Polar 示例的标记读数是幅值与相位：第 107 页原图显示 `493.907 mU` 和 `-65.37°`。结合 Default 随迹线格式动态调整的说明，本项目采用这一默认读数映射；手册没有逐格式列出 Default 映射表，因此这是基于官方示例的推断。显式 Marker Format 的 SCPI 枚举 `POLar` 在第 1034 页则对应 Real Imag，不能把它等同上述默认映射，也不能因此改写 Keysight 的圆图标记查询。[第 95、107、440–441、1034 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=107)

默认 Polar 标记的后端值为 `|z|` 与相位（度），正式读数和逐点拖动预览采用同一计算。曲线坐标及标记定位的原始实部 / 虚部保持独立；零幅值的幅值仍是有效 0，相位为未定义，不伪造 `0°`。参考原图保存于临时目录的 `um-v74-p107-marker-0.png`。

## 图窗、迹线与布局

| 操作 | 官方语义及依据 |
| --- | --- |
| Trace Config 的 Add Trace | 复制活动迹线到同图并激活；Trc 编号取最小空闲正整数。[第 448 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=448) |
| Trace Config 的 Add Tr+Diag | 复制活动迹线到新图；不是只移动旧迹线。[第 448 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=448) |
| Display 的 Add Tr+Diag | 新建图和默认迹线；继承当前通道，图号为现存最大值加一。须与上一入口区分。[第 794 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=794) |
| 移动 / 隐藏 | Diagram 选择器或拖动迹线条移动到另一图；Show 单独控制可见性，不删除定义。[第 57、448 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=448) |
| Delete Trace | 删除活动定义；若是该图唯一迹线也删图。整个设置最后一条迹线不能删。[第 448–449 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=449) |
| Delete Diagram | 删除图及其全部迹线；剩余图连续重编号，最后一图禁止删除。[第 795 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=795) |
| Overlay All | 全部迹线合入一张最大化图，允许混合坐标格式。[第 796 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=796) |
| Split All | 每迹线创建独立图，删除原图布局；不是四窗轮流分组。[第 796 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=796) |
| Dual / Triple / Quad Split | 分为指定数量的图，尽可能分离不同格式/通道；迹线不够则创建默认迹线。[第 797–798 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=797) |
| Maximize Diagram | 当前图最大化或恢复原布局，保留图内全部迹线；双击图的快捷行为另外隐藏该图其他迹线，二者不可混淆。[第 65、795 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=65) |

图窗内槽号、`Trc<n>` 名称和测量定义不是同一身份。R&S 原生命令 `DISP:WIND<n>:TRAC<m>:DELete` 仅解除显示关联，不删除定义；`FEED` 显式分配图内槽号，而 `EFEed` 在现行固件可移动已有迹线。以上用于理解领域关系，不能直接替换本项目的 Keysight 命令合同。[第 1226–1228 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=1226)

活动迹线在标题中高亮，活动迹线决定坐标轴；已核对相关段落没有规定曲线绘制顺序。[第 78、91、796 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=78) 用户要求“活动迹线置顶”可作为明确的绘图规则实现，不应写成已查到的原机规范。

## 标记的新增、选择与删除

- Add Marker 使用下一空闲编号；编号按钮创建或启用对应成员。[第 500–502 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=500)
- 点击符号或读数行激活标记。直接拖动符号定位在正文中限定 Cartesian；已有 Smith 屏幕吸附拖动属于项目额外便利功能。[第 93 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=93)
- 垂直拖单个标记到垃圾桶是关闭它；拖整组读数到垃圾桶关闭该组及关联标记功能。[第 56、502 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=56)
- On 关闭当前成员，All Off 关闭活动迹线全部成员；关闭保留 Marker Props，删所属迹线才彻底丢弃属性。因此 UI 的删除标记不应清空并销毁整个集合。[第 501–502 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=501)

R&S 原生命令 AOFF 的第 1031 页说明移除活动 recall set 所有迹线的标记，并明确保留 stimulus、format、delta mode、number；它与 GUI All Off 的范围描述有区别。本项目维持 Keysight 的当前所选测量范围，不能只凭 R&S 同名命令扩大副作用。[第 1031 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=1031)

## 对当前项目的整改含义

当前四图窗预算、不同坐标强制分图、只允许反射参数选 Smith、启动 HOLD、物理参数要求先 HOLD，均应作为项目现有限制识别；不能把它们当成 ZNB 官方规则。追求一致交互时，应按上述领域关系整改，由 C++ 保存图窗及归属、逐迹线格式与标记，前端只提交意图和渲染。

实际驱动只有完整帧和有限取消能力时，保留明确的配置事务、生效版本和新帧身份；商业交互不构成在原生调用中途竞争修改配置的理由。

本地 PDF SHA-256：`874901ee4e880dbca9a606406df448494f610233f0cd3b6a61244464b19fdccd`。原生 Format、Diagram、Split 菜单图分别提取至既有临时目录的 `um-v74-p435-controls-0.png`、`um-v74-p794-controls-0.png`、`um-v74-p797-controls-0.png`；它们仅用于对照，不成为运行资源。
