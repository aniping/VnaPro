# VnaCalibration

纯复数校准模块，不依赖 Core、Service、Display 或协议。C++14 接口位于 `include/vna_calibration/calibration.h`。

- `SolveThreeStandards` 接受任意三组实测/定义复数反射，求 D/S/R，不限于理想开短负载。
- `SolveKnownThru` 使用两端反射系数、已知二端口 Thru 及隔离，求双向负载匹配和传输跟踪。
- `CorrectOnePort` 实现 `(m-D)/(R+S*(m-D))`；`CorrectTwoPort` 联合校正完整十二项模型。
- `CorrectFourPort` 对完整四源帧的 16 项执行 `A=(M-offset)/tracking`、`B=I+match⊙A`、`S=A*inverse(B)`；最后一步为真正 4×4 矩阵乘法，不拼接独立双端口结果。
- `CalibrationData` 保存频轴、物理端口和逐点系数；`Covers` 判断覆盖。`CalibrationPtr` 是不可变读取快照。

失败返回 `CalibrationError`，不修改输出、不抛异常；相对分母门槛为 `1e-12`，拒绝非有限输入/输出。文件兼容、采集事务、启停和标准模型由调用者负责。

`calibration_math` 的 20 组固定向量取自解包 Snau/Ecal 原机器码仿真输出。生成记录位于 `reference/analysis/vnapro-calibration`，原分析位于 `reference/analysis/sna`。公式、角色及系数顺序保持一致，错误策略按项目有效性合同实现。接入与操作见[校准接入与使用](../docs/校准接入与使用.md)。

四端口模型固定物理 P1～P4，`port1=1,port2=4` 表示完整四口，`fourPortTerms` 每频点为 receiver-major 的 16 格 `[offset,match,tracking]`；对角为 D/S/R，非对角为 X/L/F。无隔离时 12 个 X 为零，即 36 个拟合项及 12 个零存储槽。新增固定原 `040D80` 四口黄金向量及独立双端口拼接反例；证据和边界见[实现依据](../docs/research/四端口TOSM实现依据.md)。奇异与非有限输入继续按产品合同明确拒绝，异常路径不宣称与原程序逐行为相同。

`four_port_mechanical_golden.h` 另固定原机械完整黄金链中的乱序物理口、非零隔离及非理想标准一组数据。回归通过当前 OSL/已知 THRU 核重建原 48 项，再核对两个独立 DUT 的完整 16S；夹具记录原产物与 Snau SHA256、source/receiver 映射。该 math 回归不扩展服务当前只开放理想标准的范围。
