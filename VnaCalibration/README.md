# VnaCalibration

纯复数校准模块，不依赖 Core、Service、Display 或协议。C++14 接口位于 `include/vna_calibration/calibration.h`。

- `SolveThreeStandards` 接受任意三组实测/定义复数反射，求 D/S/R，不限于理想开短负载。
- `SolveKnownThru` 使用两端反射系数、已知二端口 Thru 及隔离，求双向负载匹配和传输跟踪。
- `CorrectOnePort` 实现 `(m-D)/(R+S*(m-D))`；`CorrectTwoPort` 联合校正完整十二项模型。
- `CalibrationData` 保存频轴、物理端口和逐点系数；`Covers` 判断覆盖。`CalibrationPtr` 是不可变读取快照。

失败返回 `CalibrationError`，不修改输出、不抛异常；相对分母门槛为 `1e-12`，拒绝非有限输入/输出。文件兼容、采集事务、启停和标准模型由调用者负责。

`calibration_math` 的 20 组固定向量取自解包 Snau/Ecal 原机器码仿真输出。生成记录位于 `reference/analysis/vnapro-calibration`，原分析位于 `reference/analysis/sna`。公式、角色及系数顺序保持一致，错误策略按项目有效性合同实现。接入与操作见[校准接入与使用](../docs/校准接入与使用.md)。
