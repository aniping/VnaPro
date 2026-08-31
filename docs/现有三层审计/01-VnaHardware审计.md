# 现有三层审计 01：VnaHardware

> [返回审计入口](../现有三层架构审计与整改方案.md) · 下一篇：[VnaAcquisition 审计](./02-VnaAcquisition审计.md)

## 1. 结论

底层工程基础可以迁移，但当前公开契约无法证明真机数值正确，也无法隔离永久阻塞驱动。目标归属是 L1 驱动开发包与隔离宿主、L2 硬件适配；通用契约下沉 L0。

## 2. 保留

- C++ 主程序与插件间使用 C ABI，动态库异常不直接跨边界。
- Prepare 与 Execute 分开，Prepared 结果不可变；布局、执行报告和能力有显式类型。
- Actor 与 Worker 分离，命令队列、事件队列、波块池及 Prepared payload 都有局部上限。
- 波块按 Layout、Sequence、范围和覆盖校验，Abort 首错处理已有专项测试资产。
- 插件加载使用目录 fd、`O_NOFOLLOW`、`RTLD_LOCAL` 等约束，生命周期顺序清晰。

这些实现应迁移，不应推倒重写。

## 3. 问题

### P0

以下三项分别是四个中央 P0 中“数值契约”“驱动阻塞”和“实际值回传”在 Hardware 的层内证据，不是新增三类中央 P0。

- ABI 波值没有逐 series/span 的机器语义，真机物理语义不可证明；详见 [跨层 P0 1.1](./04-跨层P0问题.md#11-波形只有复数没有可执行数值契约)。
- 同步驱动调用配合无界 `join()`，可能挂死整个进程；详见 [跨层 P0 1.3](./04-跨层P0问题.md#13-驱动可无限阻塞关闭)。
- 请求计划冒充实际计划，设备量化后会产生校准和横轴错误；详见 [跨层 P0 1.2](./04-跨层P0问题.md#12-允许量化却不能回传实际值)。

### P1

- `VnaHardwareManager.cpp:96-169` 每次 open 都创建新会话，只登记 weak_ptr。
  - 风险：同一设备可能被重复控制；Host 已独占但响应丢失时，无法精确恢复。
- `VnaHardware.h:21-40` 的命令返回 ID，终态依赖单一 Sink，而且事件可以早于返回。
  - 风险：调用方还没有建立上下文，终态已经到达。
- `VnaEventDispatcher.cpp:34-75` 让控制事件和移动数据共用单订阅者；没有订阅时 post 失败。
  - 风险：终态可能丢失，且无法提供独立观察能力。
- `VnaEventDispatcher.cpp:120-134` 在 Sink 异常后只停用订阅。
  - 风险：生产者继续运行，但已经没有消费者接收结果。
- Discover sink 是借用引用，deadline 不拥有回调寿命。
  - 风险：取消、析构和回调之间的生命周期由上层猜测。

### P2

- `DeviceDescriptor`、Error 和 Result 等通用类型位于 Hardware。
  - 风险：上层纯核心被迫依赖整个硬件抽象层。

## 4. 整改

整改只保留六个目标结果：

1. 公共数值语义下沉到 L0，驱动必须报告硬件实际接受的设置。
2. 每台设备只有一个可查询的会话权威；准备结果以不可伪造、只能转移一次的执行能力交给采集层。
3. 控制状态与大块数据分开交付；消费者失败必须成为可观察终态，不能只停用回调。
4. 发现、准备、执行和关闭都有截止时间；不可信第三方驱动进入独立进程故障域。
5. 旧 ABI 只在精确、不量化的受限配置中兼容，不能冒充完整商用设备能力。
6. 通用 Result、Error、单位和身份迁入 `VnaContracts`，硬件层不再成为上层公共类型库。

R1 完成公共契约、实际计划、设备唯一所有权和驱动隔离；R2 再让 Acquisition 只消费独占执行能力。字段和驱动隔离以 [D1](../详细设计/01-公共契约与硬件采集.md) 为准，部署交接以 [D4A](../详细设计/04A-应用会话与事务.md) 为准，结果恢复与故障注入以 [D4D](../详细设计/04D-持久化结果与恢复.md) 为准。

## 5. 验收

### 5.1 数值 Schema

- **必测**：同帧不同单位、归一化、相干组、partial，以及 Segment 中途模拟路径变化。
- **通过**：
  - bind 按 series/span 明确拒绝；span 完整覆盖；没有错误数值流出。
  - Raw 数据没有逐 series/span WaveSchema、span 覆盖不完整，或后续 Ratio/Cal Evidence/Result 没有匹配的 NumericSchema 和 upstream schema 链时，均无法构造。
  - 真实驱动接入清单回答每个字段和每个变化边界的来源。

### 5.2 实际计划

- **必测**：频率、IFBW、功率量化和物理重排。
- **通过**：Result 轴与 provenance 使用 actual，request-effective 映射可追溯；Observation、Cal compatibility 和 Trace x 轴均使用实际值；超容差变化被拒绝或明确降级。

### 5.3 设备所有权

- **必测**：同一 DeviceId 并发 open。
- **通过**：恰有一个 Lease 成功，释放后可以重新打开。

### 5.4 驱动故障

- **必测**：Discover、Prepare、Execute、Abort 永不返回，以及 Host 崩溃。
- **通过**：deadline 内得到唯一故障并完成 Close；应用能够继续运行或关闭，测试进程本身不挂死。

### 5.5 代次

- **必测**：Hardware Prepare 在途时再次编辑、取消后晚到成功、候选失败、旧帧晚到。
- **通过**：同一 DeviceSession 最多一个 Prepare 在途；旧 handle revoke/release；旧 revision 不被标为当前结果。
