# ZNB 慢扫局部刷新行为核对

核对日期：2026-10-16。用户确认参照 R&S ZNB，沿用本项目的 User Manual v74 / FW V4.25 界面基线。慢扫局部刷新涉及真实采集进度，不能将完成的整帧按计时器分段播放当作实时测量。

## 官方行为与适用边界

| 项目 | 已证行为与来源 | 对本项目的约束 |
| --- | --- | --- |
| 逐点形成迹线 | Alternated 按方向完成部分扫；Chopped 在每个频点完成所需全部部分测量再前进，从扫描开始逐点形成迹线。Auto 对快扫选前者、慢扫选后者，Chopped 通常更慢。见 [ZNB v74，第 79–80、697 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=79)。 | 新显示的校正点必须已收齐所需工作源的数据，不能以单个方向代替四端口校正结果。 |
| 慢扫位置指示 | Show Sweep Symbols 显示向下指向迹线的箭头，典型条件为高点数或窄测量带宽。见 [第 814 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=814)。 | 采用向下箭头表达实测推进位置；手册未给精确出现阈值、尺寸或颜色。 |
| 保留前次结果 | Restart: Set all Traces to 0 默认关闭，Restart Sweep 保留前次测量数据；打开才删除。见 [第 814 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=814)。 | 重扫不默认清空前次曲线；具体新旧边界擦除方式尚无证据。 |
| 状态栏 | 展示准备状态和扫描进度；太快而无法观察时用移动色渐变。见 [第 49、79 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=49)。 | 状态必须由真实采集事件驱动。 |
| Marker 跟踪 | 搜索 Tracking 通常每扫更新；Sweep Progress 搜索模式才每点更新。见 [第 507–508、515 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=507)。 | 这是搜索重定位规则，不能解释为普通固定位置 Marker 的读数也必须等整扫。 |

固定 PDF 的 SHA-256 为 `874901ee4e880dbca9a606406df448494f610233f0cd3b6a61244464b19fdccd`，与仓库 `docs/reference/znb8/source-manifest.json` 一致；已按完整 PDF 核对上述页面。第 813 页设置图能证明示例勾选了 Show Sweep Symbols，但没有提供扫频箭头的可量化原生特写，因此不推测其像素外形。

## 本次实现合同

首轮局部刷新**不改变硬件扫描次序或批大小**，不插入视觉效果所需的采集等待。利用 VN10 现有每批最多 64 点、收齐全部所需工作源后的真实分块回读，公开已完成的连续频点前缀；快速扫描继续使用整帧结果。2026-10-17 按用户要求增加的手动软件扫时允许缩小主机 Get 批大小，Auto 仍沿用原有 64 点粒度；这是另行授权的软件节拍，详见 [扫频时间与软件节拍](扫频时间与软件节拍.md)。分块能力来自本项目驱动，不等于实现了 ZNB 的 Chopped 硬件调度。

2026-10-17 修订首次启用条件：Auto 必须已实际回读至少 **1 秒**且预估剩余至少 **0.5 秒**；手动目标至少 1 秒且预估剩余至少 0.5 秒时可提前显示。启用后的刷新节流仍为 **100 ms**。这些均为 VnaPro 的实现取舍，**不是已公开的 ZNB 固件参数**。预览只在实际频点到达后推进；同一采集配置下保留上一完整扫的未扫尾部，无完整前扫时未到点留空。配置改变后不拼接旧频轴、旧功率或旧校准的结果。

扫频指示采用 8×14 设计像素的细竖杆与开放 V 形向下箭头，不填充、不接收鼠标事件；尖端仍落在真实已采末点，颜色随迹线。它与 Marker 的实心三角及编号区分，尺寸和线宽属于本项目视觉选择，不宣称为原厂精确外形。

SCPI 完整数据查询、导出和测量完成屏障继续使用完整帧；固定位置 Marker 读数也保留完整帧语义，避免半扫预览被误认为完整测量。这是本项目的安全边界，**不是手册对所有 Marker 读数的原厂规定**。R&S 的远控应用说明要求自动程序与扫频完成同步后读取可靠结果，见 [1EF62_1E，第 6–7、10 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/dl_application/application_notes/1ef62/1EF62_1E.pdf#page=6)；其中新旧扫混合图属于频谱测量例图，不能用作 ZNB 擦除外形证据。

## 不能宣称已经 1:1 的细节

ZNB 未在所核对手册中公开 Auto 切换时间、箭头出现阈值、显示帧率、像素尺寸、新旧曲线交界间隙或中止半扫的保留方式。现有证据支持逐点推进和慢扫箭头等行为合同；这些未知细节需要同型号同固件录屏或实机观察后才能声称 1:1。

对照资料：[Keysight Sweep Settings](https://helpfiles.keysight.com/csg/NA520xA/S1_Settings/Sweep.htm) 明确给出其扫描指示符在 0.3 秒或 Point 触发时出现，且定位到接收机最后实测点；Point Sweep 测齐各参数才更新。这是型号差异，不能把 **0.3 秒**套作 ZNB 阈值。[Keysight Trigger](https://helpfiles.keysight.com/csg/NA520xA/S1_Settings/Trigger.htm) 还规定多端口校正须齐备各方向才更新，其触发规则也不能与 ZNB Auto 调度混为一谈。
