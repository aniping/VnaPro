# 四端口 TOSM 实现依据

核对日期：2026-10-16。范围是四个物理端口的理想 O/S/M、全部六对匹配互易理想 THRU，以及可选的逐对隔离。未实现未测连接补边、Unknown THRU、ECal、非理想标准件模型或校准频轴插值。TOSM 是项目采用的外部方法名，不冒充 Snau 原程序中的枚举名称。

## 标准、表示与求解

四口各测 Open/Short/Match 共 12 次连接，六个无向端口对各接一次 THRU，合计 18 次；每个 THRU 捕获同一完整帧中的两个方向，不能拼接不同轮次。官方 R&S ZNB/ZNBT User Manual 72 的 4.5.1.4、印刷第 168 页也列出四口 18 次连接、24 个方向测量、36 个误差项，并注明不含隔离项。[官方手册](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_ZNBT_UserManual_en_72.pdf#page=168)。本次直接读取该链接返回 404，上述表格由搜索引擎的该官方 PDF 文本缓存核对；商用仪器减少 THRU 的连接优化不属于本实现子集。

本实现逐频点保存 receiver-major 的 4×4 矩阵，每格三个复数：对角 `[D,S,R]`，非对角 `[X,L,F]`。这是 **48 个复数存储槽**；未测隔离时 12 个非对角 X 明确为零，剩余 36 项由标准求解。启用隔离另需六次逐对连接，捕获 12 个方向的 X，合计 24 步；不能将 48 个槽位称为官方无隔离 TOSM 的 48 个独立误差项。原表布局见[服务原分派报告](../../reference/analysis/multiport-calibration-20261015/service/source-dispatch.md)，物理索引和 D/S/R/X/L/F 顺序回溯至 [0339B0](../../reference/analysis/sna/ghidra/snau/calibration/1400339b0.c)、[037210](../../reference/analysis/sna/ghidra/snau/calibration/140037210.c)。

逐口反射使用三标准闭式 `035DC0`。逐对已知 THRU 使用 `037210` 的匹配互易标量模型，本次仅开放 `t=1`；不能把它声明成任意不匹配、非互易的完整四 S 参数标准。当前一般已知 THRU 公式代入 `S11=S22=0、S12=S21=1` 后与该良态标量模型一致。公式、实际输入波量和边界见[三标准及标量 THRU 报告](../../reference/analysis/multiport-calibration-20261015/scalar/source-scalar-analysis.md)，原始算术见 [035DC0 C](../../reference/analysis/sna/ghidra/snau/calibration/140035dc0.c)、[037210 C](../../reference/analysis/sna/ghidra/snau/calibration/140037210.c) 及各自同名 ASM。

对应原指令证据是 [三标准 160 例](../../reference/analysis/multiport-calibration-20261015/scalar/original-closed-osm-validation.json)、[已知 THRU 160 例及边界](../../reference/analysis/multiport-calibration-20261015/scalar/original-scalar-thru-validation.json)。它们证明记录范围内的数值核，不等于证明完整仪器流程或所有异常输入。

## 十六参数必须联合校正

从同一不可变完整四源帧取 `Mij=b_i/a_j`，逐频点构造：

```text
Aij = (Mij - offsetij) / trackingij
Bij = δij + matchij * Aij
S   = A * inverse(B)
```

第二行是逐元素乘，第三行是真正 4×4 矩阵运算；不能分别调用六套二端口校正后拼成十六参数。原 [040D80 C](../../reference/analysis/sna/ghidra/snau/correction/140040d80.c) 的求逆、矩阵乘与布局由[数学原行为报告第 7 节](../../reference/analysis/multiport-calibration-20261015/math/source-math-analysis.md)核对；[活动端口/容量 oracle](../../reference/analysis/multiport-calibration-20261015/math/original-multiport-capacity-validation.json) 包括 N=1/2/3/4 与反序、非连续物理端口。其 N=4 记录中，逐 pair 方案与联合解差异可达约 0.56。

服务会话按步骤完整端口对定位样本；每个标准使用新 run 的完整帧，结束 Stop 后才确认。求解生成独立候选，全频点全部通过后一次发布；失败保留之前完成的校准集及可重试会话。校正必须覆盖所有请求的 S 参数，不能回退原始数据并标记已校准。

## 与原程序的兼容边界

原三标准/THRU 核把特定小分母替换为正实数 `1e-30`；`040D80` 未检查 `0443E0` 返回失败便继续乘法。本项目沿用现有有限性、tracking 和病态矩阵拒绝策略，失败不发布部分系数或部分数组。这是明确的产品失败政策差异，不能称为异常边界逐指令 1:1；原观察见[标量报告](../../reference/analysis/multiport-calibration-20261015/scalar/source-scalar-analysis.md)及[逆矩阵边界记录](../../reference/analysis/multiport-calibration-20261015/scalar/original-inverse-boundary-validation.json)。

新增[服务测试](../../VnaService/tests/four_port_calibration_test.cpp)使用独立前向方程生成非单位、非零匹配项的 18/24 个标准，再核对全部 48 个存储槽，并用另一个稠密 DUT 验证实际采集、选集与十六参数校正；它不调用生产校正函数生成测量输入。另验证能力越界、非法导入、端口对隔离、坏标准重采和失败保旧集。该产品回归与原指令 oracle 必须分别报告，不能相互冒充。

研究对话后续补充了 [机械四口理想标准脚本](../../reference/analysis/multiport-calibration-20261015/service/round10/probe_four_port_ideal.py)及[原程序结果](../../reference/analysis/multiport-calibration-20261015/service/round10/four-port-ideal-validation.json)：12 OSL 加六对双向理想 Defined THRU，显式执行 `00E7E0→0160E0→008B50→00E370/040D80`，两种物理口序、三频点，每点保存 48 个复数槽及 16S，六次应用最大误差约 `4.34e-18`。其系数接近单位误差 `[0,0,1]`，且使用 Defined selector 2，未执行 Flush selector 1；因此证明的是 t≈1 时的组件连接和排列，不能扩大为非平凡误差、GUI 发布或设备扫描事务。

指定研究对话随后补齐 [round11 非平凡机械黄金链分析](../../reference/analysis/multiport-calibration-20261015/service/round11/nonidentity-mechanical-golden-analysis.md)及[原始结果](../../reference/analysis/multiport-calibration-20261015/service/round11/nonidentity-mechanical-validation.json)：两种口序 × 隔离零/非零，共四个主场景、三频点、每点两个独立 DUT，原业务链实际写满 48 个复数槽后登记、映射并调用原联合校正；系数最大误差约 `4.45e-16`，24 次 DUT 应用最大误差约 `2.29e-16`。反转 source/receiver 或交换隔离方向的负对照会明显偏离。raw8 的 forward 隔离是 `X(q,p)`、reverse 是 `X(p,q)`，与本项目 receiver-major 的 `(q,p)` / `(p,q)` 布局一致。

该新黄金链使用原模型产生的非理想 OSL 和 matched reciprocal、`t≠1` 的 THRU。数学核支持这些已知标准输入，但当前服务仅开放理想 Γ=`1/-1/0` 和 `t=1`，不能直接把新非理想标准测量灌入服务并期待相同系数；服务层仍由其理想标准独立前向回归验证。原组件黄金链也不证明完整 GUI、设备扫描事务或硬件精度。已暂停的 `plan.md`、`math/evidence.md` 和 `suspended-implementation` 仅作历史记录，当前实现依据以上原行为报告和所链接 C/ASM/oracle。

产品 `calibration_math` 已接入[固定机械黄金夹具](../../VnaCalibration/tests/four_port_mechanical_golden.h)，取 `[3,1,0,2]` 非零 X 场景的 11 MHz 点；当前数学核从原 OSL/THRU 输入重建全部 48 项，再联合校正两组原 DUT，均对原输出通过。夹具 SHA256 与来源 JSON 已核对一致；原外部 source-major 输入明确重排为物理 receiver-major，避免沿用旧字段反名。
