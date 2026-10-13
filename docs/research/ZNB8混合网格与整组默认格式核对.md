# ZNB8 混合网格与整组默认格式核对

核对日期：2026-10-13。主要依据为 [R&S ZNB User Manual v74 / FW V4.25](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf)。页码为 PDF / 正文页；以下区分正文规定、官方原图与项目实现约定。

## 同一图窗的混合格式与背景

| 证据 | 可确认的行为 | 不应扩大的结论 |
| --- | --- | --- |
| 第 796 页 Overlay All 正文 | 全部迹线可合入一个图窗，允许 Cartesian 与复平面格式共存；坐标轴刻度对应活动迹线。 | “轴刻度对应活动迹线”不等于“背景只绘制活动迹线的一种网格”。 |
| 第 796 页 Overlay All 原图 | `Trc1 S21 dB Mag`、`Trc2 S11 dB Mag`、`Trc3 S21 Smith` 同图；矩形网格与 Smith 网格同时存在。活动迹线为 Trc1，左侧给出 dB 刻度；Smith 的归一化网格标签仍可见。 | 该原图未展示 Smith 为活动迹线、Polar 与 Smith 并存或多条圆图迹线具有不同 Ref 值时的精确取舍。 |
| 第 104–106 页 Trace formats | 格式为逐迹线设置；Cartesian 将频率等激励量映射到水平轴、标量响应映射到垂直轴；复平面格式将响应的实部与虚部映射到水平与垂直方向。 | 不得为叠加方便把 Smith 曲线改画成频率曲线，或把 dB Mag 曲线改画成复平面曲线。 |
| 第 106–110 页 Complex trace formats | Polar、Smith 与 Inv Smith 的曲线都是复平面响应；它们的背景网格分别表达幅值 / 相位、归一化阻抗、归一化导纳。 | Inv Smith 的网格变化不意味着对迹线复数取负；不能把三类网格合并为一个 Smith 背景。 |
| 第 112、435 页格式适用说明 | 测量量和格式允许任意组合，因此 S21 的 Smith 曲线也可与 dB 曲线共存。传输量以 Smith 显示时不具有真实反射阻抗解释。 | 不能只允许 S11/S22/S33/S44 选择 Smith。 |

第 796 页原图背景与两类网格采用统一的黑底、灰蓝网格风格，各迹线保持自己的颜色；网格不随各条迹线分别染色。活动迹线决定轴刻度，迹线的参考标识属于其迹线显示信息，而不是把整幅背景改成迹线色。[第 90–91、796、799–802 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=796)

本次修正应据可见迹线的格式绘制所需背景，而不能用全局活动格式排他地选一个背景；切换活动迹线不应令其他可见格式的背景消失。各迹线仍按自己的格式和 C++ 保存的刻度绘制，前端不修改仪器状态。

官方已核验页没有明示同一种圆图格式有多个不同 Ref 值时是否绘制多份背景，也没有给出混合 Smith、Inv Smith、Polar 的网格叠加次序。项目需要明确并测试稳定的去重及代表迹线选择规则；该部分属于实现约定，应与“原机已有明文”分开表述。

本项目以可见的后端呈现快照派生背景，矩形网格绘制一次，圆图按格式与最终投影半径去重。圆图为主坐标时沿用主圆图半径；矩形为主坐标时各圆图沿用自己的保存半径，背景与曲线、标记和命中使用同一几何。背景全部先画，再按原目录绘制非活动迹线，活动迹线最后绘制；空帧不影响背景，隐藏迹线不引入背景。这些去重及绘制次序属于项目合同，不在前端保存新的仪器控制状态。

### 刻度文字与图窗尺寸

仓库保存的官方原图 `docs/reference/znb8/manual/um-v74-p090-single-cartesian.png` 和 `gs-v70-p037-main-quad.png` 可见：刻度数字紧贴黑色底面，背景网格在字框处被打断。项目据此统一圆图与矩形刻度的 `Tahoma, Arial, sans-serif` 字体，并使用不透明的图表底色遮挡文字范围，避免半透明块或描边造成模糊。混合圆图的全部背景及迹线完成后再绘制坐标标签，后画网格不能穿过先画数字。

手册没有公布轴字号或自动缩放像素阈值。本项目按绘图区宽、高相对于 320×250 的较小比例派生轴字号，基础字号限制在 9～11 px；大窗保持 11 px，4×4 小窗使用 9 px，再乘以 C++ 保存的用户字号比例。标签行高随实际字号变化，空间不足时仅减少文字密度，保留参考值和十格网格。标题与通道行维持已有字号合同，频点、刻度值、参考半径和显示状态仍来自 C++，这些像素适配不发送控制命令。

## All S-Params 与普通单参数默认格式

这两种默认不能混为一谈：

1. 普通 S 参数属于复数、无量纲量，其通用默认格式为 dB Mag；修改测量量后采用相应默认格式。这在第 112 页推荐格式表和第 436 页 dB Mag 说明中有明确文字。[第 112、436 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=436)
2. 第 290 页 All S-Params 规定完整 S 参数集合各一图窗，并按 n×n 排列；该段没有逐项写出新迹线的默认格式。[第 290 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=290)
3. 第 44 页 ZNB 官方界面原图展示二端口 2×2 矩阵：左上 S11 与右下 S22 为 Smith，非对角 S12/S21 为 dB Mag。Smith 标题为 `200 mU / Ref 1 U`，dB Mag 标题为 `10 dB / Ref 0 dB`。这是当前 ZNB 手册的直接界面证据；该图未写明由哪一步操作生成，不能仅凭它证明 All 动作的全部副作用。[第 44 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=44)
4. R&S ZNC/ZND 官方手册 v37 的 All S-Params 段落明确规定：主对角反射采用 Smith，非对角传输采用 dB Mag。R&S ZVA/ZVB/ZVT 官方手册 v33 的同名动作也有对应规定。这是同厂商商业界面的旁证，应标明机型，不能冒称是 ZNB v74 该段正文。[ZNC/ZND 第 190 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9557_01/ZNC_ZND_UserManual_en_37.pdf#page=190)、[ZVA/ZVB/ZVT 第 131 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_common_library/dl_manuals/dl_user_manual/ZVA_ZVB_ZVT_OperatingManual_en_33.pdf#page=131)

用户本次明确要求 All S-Params 的反射图使用 Smith。结合当前 ZNB 官方 2×2 原图和上述同厂商旁证，本项目整组动作采用下列合同：

```text
S11 Smith   S12 dB Mag   S13 dB Mag   S14 dB Mag
S21 dB Mag  S22 Smith    S23 dB Mag   S24 dB Mag
S31 dB Mag  S32 dB Mag   S33 Smith    S34 dB Mag
S41 dB Mag  S42 dB Mag   S43 dB Mag   S44 Smith
```

二端口仅创建左上 2×2 的四个合法成员。C++ 在同一次整组事务中按接收端口等于源端口选择 Smith，否则选择 dB Mag；不得由 WebUI 在收到整组结果后再逐条补发改格式请求。普通 Meas 单参数修改和普通新建迹线的通用默认不因此全局改成反射 Smith。

确认、删除旧定义、原子重建、保留扫描配置及全局显示偏好的既有合同保持，只有整组新成员的逐迹线默认格式调整；圆图采用其格式专用默认刻度，而非继承旧同名迹线的刻度或 Marker。

## S-Parameter Wizard 是不同功能

第 291 页明确说，Wizard 的传输参数总用 dB Mag，反射参数可选择 Smith 或 dB Mag；第 292 页明确 Wizard 为获得可预测结果重置除当前校准数据外的设置。这属于独立向导，不能用来证明 All S-Params 必须重置扫频范围、点数、带宽或当前运行方式。[第 291–292 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=291)

## 验证范围

- 混合 Smith / dB Mag 时，无论激活哪条迹线，两类可见背景均存在；圆图曲线保持复平面坐标，矩形曲线保持激励 / 标量响应坐标。
- 仅有一种可见格式时不绘制无关背景；隐藏、改格式、移出或删除最后一条某类型迹线后移除其背景。
- Polar、Smith 与 Inv Smith 的背景不能混用；同类背景去重规则、代表迹线选择及不同 Ref 值的稳定性须有明确项目测试。
- 二 / 四端口整组重建验证对角 Smith、非对角 dB Mag；HTTP 快照与 SCPI 格式查询读到同一 C++ 状态，刷新后一致。
- 整组后扫描配置与运行身份保持，驱动及模拟器数据合同不变。

本轮结果：前端 262 项、CTest 53 项和生产构建通过。混合背景测试先捕获两种主坐标下均漏画另一类背景，再验证去重、无帧背景、隐藏格式及活动迹线置顶；Smith / Inv Smith 小单位圆减少文字密度而保留全部网格和原投影。实际浏览器确认原场景双背景、切换主迹线的轴刻度、二 / 四端口整组的对角 Smith 与传输 dB Mag，以及刷新保持；HTTP / SCPI 用独立同帧原始波比值核对格式投影，原有主场景已恢复。

本地 v74 PDF SHA-256：`874901ee4e880dbca9a606406df448494f610233f0cd3b6a61244464b19fdccd`。本次官方原图从 PDF 原始图片提取到临时参考目录：`mixed-um74-p796-0-I1.png`（混合背景）、`mixed-um74-p796-1-I2.png`（Split All）、`mixed-um74-p44-1-I2.png`（2×2 Smith / dB 矩阵）；仅供核对，不作为 WebUI 运行资源。
