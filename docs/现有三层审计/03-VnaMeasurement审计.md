# 现有三层审计 03：VnaMeasurement

> [返回审计入口](../现有三层架构审计与整改方案.md) · 上一篇：[VnaAcquisition 审计](./02-VnaAcquisition审计.md) · 下一篇：[跨层 P0 问题](./04-跨层P0问题.md)

## 1. 结论

现有代码是可保留的 Observation 纯核心，不是完整网络测量层。目标是重定位到 L4 `VnaObservationCore`，并由 L4 Network Model 和 L5 Channel 编译继续向上组成产品测量能力。

## 2. 保留

- `compile → bind → build` 是正确的冷/热路径分离，且没有额外线程或 I/O。
- Measurement 定义同时推导 `requiredWaves` 和解释返回数据，避免两份定义漂移。
- Prepared 绑定和 Frame 均不可变，容量/溢出/缺输入/非有限/零分母有明确质量位。
- 文档已经正确说明“波比值不自动等于 S 参数”。

## 3. 问题

### P0

以下两项是四个中央 P0 中“数值契约”和“实际值回传”在 Measurement 的层内证据，不是新增两类中央 P0。

- `ObservationBuilder.cpp:196-214` 只按数值做除法。
  - 风险：跨归一化或非相干值仍可能被计算。
- `MeasurementRequest` 内嵌 `VnaSweepPlan`，输出仍是同一类型。
  - 风险：产品意图、硬件 Recipe 和实际计划混在一起。

### P1

- 名称叫网络测量层，实际只产 Direct/Ratio Observation。
  - 风险：上层会把结果误当成 S 参数或 Channel。
- Observation 后没有显式 provider/factory conditioning，以及端口和激励网络模型。
  - 风险：`b/a` 可能只凭索引命名为 `Sij`，factory correction 也可能被隐式吞掉。
- bind 对 effective plan 逐字段浮点全等。
  - 风险：真机量化后无法绑定。
- `ObservationSeriesView` 只公开 `pointIndices`。
  - 风险：丢失稳定 PointId、Segment 和实际 Stimulus。
- Frame 复制全部 values/status，但没有进入全局预算。
  - 风险：两层各自合法，组合后内存仍然无界。
- 公共接口用可无限复制的 `shared_ptr<const ObservationFrame>` 表达结果保活，而且 Frame 反向保留 `PreparedMeasurement → PreparedAcquisition`。
  - 风险：实时当前、历史、分析或保存暂存只要长期持有一个 Frame，就会在账本之外钉住整条准备对象图；系统既不能证明内存上限，也无法形成唯一释放或责任移交回执。
- Observation Result 只继承 acquisition run/sweep，而且没有自己的系列 Schema。
  - 风险：多 Measurement/Trace 结果无法类型化和追溯。

### P2

- `tests/MeasurementIntegration/MeasurementPipelineTest.cpp:92-135` 使用 FakeVnaHardware。
  - 风险：没有覆盖真实插件 ABI 到 Observation 的公开链。

## 4. 整改

1. bind 检查数值域、相干关系和 partial 契约；evaluate 只执行已经证明成立的依赖。
2. 把产品测量集合与激励定义作为输入意图，产出 Observation 需求；不再让同一计划类型跨越产品、硬件和实际执行阶段。
3. 将目标重命名为 `VnaObservationCore`，旧头只提供兼容别名。
4. 新增 typed `VnaProviderConditioningCore` 和已应用状态门禁。
5. 新增 `VnaNetworkModelCore`；按 span 证明后才输出 `NetworkMatrixSchema`。
6. bind 使用 request-effective 映射、AxisSchema 和字段容差。
7. 输出借用 `AxisView`，或使用 AxisSchema 加 ordinals，保留稳定 PointId、Segment 和实际 Stimulus。
8. Prepared pipeline 在执行前预估并预留全链路预算。
9. 跨异步边界只转移已记账的 `FrameLease` 或 `ResultBufferLease`；不再把裸 `shared_ptr` 当作无限期保留凭证，结果载荷也不反向保活整条 Prepared 对象图。实时当前、历史和保存暂存分别由 L6/L7 的显式租约与配额管理。
10. 输出 `ObservationSchema`；拆分 `AcquisitionProvenance` 与 `ResultProvenance`，沿 upstream schema 链追加来源。
11. 新增 Simulator 公共链集成测试，不再只依赖 FakeVnaHardware。

重命名不等于重写：

- 先创建 `VnaObservationCore` target 并迁移现有源文件。
- `VnaMeasurement` 暂时链接并 re-export 旧 API，标记 deprecated。
- 再建立 provider/factory conditioning 的显式 CorrectionState stage。
- 建立 `VnaNetworkModelCore` 的 `ObservationSchema → NetworkMatrixSchema` 边界，再由 `VnaChannelCore` 串接。
- 现有 compile、bind、build 测试原样迁移，并补 span、实际轴、factory state、provenance 和错误 `Sij` 命名场景。

接口和字段形状不在本审计重复定义，以 [D2](../详细设计/02-数值处理与通道编译.md) 为唯一详细事实源。

## 5. 验收

- 不同归一化、相干组或 partial 不兼容时，bind 明确拒绝，不产生比值。
- 量化 Simulator 返回不同实际轴后，Observation 使用实际 Axis 和映射。
- Direct/Ratio Observation 未经过 provider conditioning 和 Network Model 证明前，不得命名为 `Sij`。
- 慢消费者长期保留结果时，Frame/Result buffer 的 Credit 与 Lease 始终有上限并最终归还；公开边界不存在可无账本复制的保活凭证，也不会因保留一个结果而钉住整条 Prepared 图。
- 输出包含自身 ObservationSchema 和完整 upstream provenance。
- 现有 compile/bind/build 测试继续通过，且公共 Simulator 链覆盖真实插件 ABI 到 Observation。
