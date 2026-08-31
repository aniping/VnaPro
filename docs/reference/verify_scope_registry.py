#!/usr/bin/env python3
"""Verify VnaPro's draft ScopeRegistry and protected policy definitions.

This tool deliberately does not approve release gates or prove vendor
compatibility. It also does not inspect the source/build dependency graph; it
only protects candidate definitions and policy tables. The repository has no
protected release tag, CI run and approval record yet, so every definition
verified here remains ``DraftPendingReview``.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import shutil
import subprocess
import sys
from collections.abc import Callable
from dataclasses import asdict, dataclass
from pathlib import Path


INTERNAL_SERIALIZATION_ID = "vnapro.scope.internal-gfm-list-item.draft1"
COMPAT_SERIALIZATION_ID = "vnapro.scope.compat-pipe-fields.v1"
COMPAT_RECEIPT_SERIALIZATION_ID = "vnapro.compat-evaluation-receipt.v1"
INTERNAL_DEFINITION_SOURCE = "ProtectedGitReviewPending"
COMPAT_DEFINITION_SOURCE = "ProtectedGitReviewPending"
REGISTRY_LIFECYCLE = "DraftPendingReview"
INTERNAL_SEPARATOR = "\n---\n"

INTERNAL_REGISTRY_START = '<a id="dd-scope-registry"></a>'
INTERNAL_REGISTRY_NATIVE_HEADING = "##### 验收范围规范登记表（`ScopeRegistry`）"
INTERNAL_REGISTRY_END = "##### 厂商兼容评估键"
HISTORY_START = "<!-- scope-version-history:start -->"
HISTORY_END = "<!-- scope-version-history:end -->"
COMPAT_START = '<a id="compatibility-transcript-gates"></a>'
COMPAT_END = "## 3. 可观察语义与架构约束"
COMPAT_HISTORY_START = "<!-- compat-scope-version-history:start -->"
COMPAT_HISTORY_END = "<!-- compat-scope-version-history:end -->"
GATE_DECISION_GAP_START = '<a id="gate-decision-gap-index"></a>'
GATE_DECISION_GAP_END = '<a id="evidence-decision-index"></a>'
EVIDENCE_PORTABILITY_GAP_MAP_START = (
    "<!-- evidence-portability-gap-map:start -->"
)
EVIDENCE_PORTABILITY_GAP_MAP_END = "<!-- evidence-portability-gap-map:end -->"
SCPI_ADDRESS_SELECTION_START = '<a id="dd-scpi-address-selection"></a>'
SCPI_ADDRESS_SELECTION_END = "#### 10.1.3 优先控制：设备清除（DCL）与证据边界"
TARGET_DEPENDENCY_POLICY_START = '<a id="dd-target-dependency-policy"></a>'
TARGET_DEPENDENCY_POLICY_END = "### 12.2 分层测试门禁"

EXPECTED_SCPI_ADDRESS_BINDINGS = (
    (
        "测量通道编号（Channel number）",
        "`SessionScope{InstrumentSessionId} + NumericAddress ↔ ChannelId + ObjectIncarnationId`",
        "父会话实例必须相同；普通测量通道内容修改不改地址",
    ),
    (
        "测量量编号（Measurement number）",
        "`ChannelScope{ChannelId + ObjectIncarnationId} + NumericAddress ↔ MeasurementId + ObjectIncarnationId`",
        "测量量必须仍属于该测量通道实例",
    ),
    (
        "测量量名称（Measurement name）",
        "`ChannelScope{ChannelId + ObjectIncarnationId} + StringAddress ↔ MeasurementId + ObjectIncarnationId`",
        "容纳 SNA `<Mname>`；唯一性、比较和改名规则仍由精确产品配置决定",
    ),
    (
        "产品迹线编号（Product Trace number）",
        "`ChannelScope{ChannelId + ObjectIncarnationId} + NumericAddress ↔ TraceId + ObjectIncarnationId`",
        "迹线的所属测量通道必须与地址作用域相同；不能由当前来源反推归属",
    ),
    (
        "窗口编号（Window number）",
        "`SessionScope{InstrumentSessionId} + NumericAddress ↔ DisplayPaneId + ObjectIncarnationId`",
        "显示窗口必须属于同一会话实例",
    ),
    (
        "窗口迹线槽（Window Trace slot）",
        "`DisplayPaneScope{DisplayPaneId + ObjectIncarnationId} + NumericAddress ↔ TraceDisplayBindingId + ObjectIncarnationId`",
        "绑定必须仍连接该窗口和一条迹线；槽号不直接标识产品迹线",
    ),
)

EXPECTED_SCPI_ADDRESS_MODEL_MARKERS = (
    "AddressAtom = NumericAddress | StringAddress",
    "ProductId, ObjectIncarnationId, EntryRevision",
    "StableParentInstance",
    "从这份冻结的 SessionSnapshot 取得当前内容 revision",
)

EXPECTED_SCPI_ADDRESS_VISIBLE_SOURCE_MARKERS = (
    "**只有地址拓扑变化才推进 `AddressBookRevision`。**",
    "删除—重建的 ABA 防护",
)

EXPECTED_SCPI_PROFILE_ADMISSION = (
    (
        "VnaPro Native",
        "本项目明确的 profile、契约测试和结构化错误基准",
        "只作为 Native 合同启用，不产生厂商兼容声明",
    ),
    (
        "`ZNB/{model}/{firmware}/{options}`",
        "覆盖编号、选择、删除、重排、Recall、默认值和错误外观的真机 transcript，并绑定精确发布回执",
        "`BlockedPendingEvidence`；当前不得启用 ZNB 厂商映射，不得声称兼容",
    ),
    (
        "`SNA/{model}/{firmware}/{options}`",
        "覆盖同一行为面的真机 transcript，并绑定精确发布回执",
        "`BlockedPendingEvidence`；当前不得启用 SNA 厂商映射，不得借用 ZNB 或 Native 规则",
    ),
)

EXPECTED_TARGET_DEPENDENCY_ALLOWLIST = (
    (
        "`VnaContracts`",
        "L0",
        "无内部 target",
    ),
    (
        "`VnaDriverSdk`",
        "L1",
        "L0",
    ),
    (
        "`VnaDriverHost`",
        "L1",
        "L0、`VnaDriverSdk`",
    ),
    (
        "`VnaHardware`",
        "L2",
        "L0、L1",
    ),
    (
        "`VnaAcquisition`",
        "L3",
        "L0、L2",
    ),
    (
        "`VnaProviderConditioningCore`、`VnaObservationCore`、`VnaNetworkModelCore`、`VnaCalibrationCore`、`VnaTransformCore`、`VnaTraceCore`",
        "L4",
        "L0",
    ),
    (
        "`VnaChannelCore`",
        "L5",
        "L0、L4",
    ),
    (
        "`VnaRuntimePorts`",
        "L6 稳定合同",
        "L0",
    ),
    (
        "`VnaDeploymentAccessPorts`",
        "L6 部署访问合同",
        "L0、L2 公开执行合同、L5 公开部署描述合同",
    ),
    (
        "`VnaRuntime`",
        "L6；含唯一的 `RuntimeStageStateStore`",
        "L0、L2、L3、L4、L5、`VnaRuntimePorts`、`VnaDeploymentAccessPorts`",
    ),
    (
        "`VnaRuntimeJournalAdapter`",
        "L6 出站基础设施适配器",
        "L0、`VnaRuntimePorts` 的 `GroupActivationSourceJournalPort`",
    ),
    (
        "`VnaApplication`",
        "L7",
        "L0、L5、`VnaRuntimePorts`；同层 sibling 必须逐 target 登记",
    ),
    (
        "`VnaApplicationHardwareOwner`",
        "L7 出站基础设施适配器",
        "L0、L2、`VnaDeploymentAccessPorts`、`VnaApplication` 的 `HardwarePreparationOwnerPort`",
    ),
    (
        "`VnaScpi`、`VnaUi`、`VnaAutomation`、`VnaFileIo`、`VnaDataService`",
        "L8",
        "L0、L7 Application ports",
    ),
    (
        "`VnaCompositionRoot`",
        "组装根",
        "表内全部 target 的显式清单",
    ),
)

EXPECTED_LOWER_LAYER_TYPE_DENYLIST = (
    (
        "L0 `VnaContracts`",
        "`ChannelDefinition`、`MeasurementDefinition`、`TraceDefinition/TraceId`、`DisplayPaneDefinition`、`TraceDisplayBinding`、`ProfileAddressBook`、`ScpiSelectionContext`、`ResultRef/ResultArtifact`、Repository/File DTO",
        "仅使用前述基础表示闭集；业务类型留在 L5/L7/L8 所有者",
    ),
    (
        "L1 `VnaDriverSdk/VnaDriverHost`",
        "`Channel*`、`Measurement*`、`Trace*`、`DisplayPane*`、`ProfileAddress*`、`Scpi*`、`Repository*`",
        "版本化设备命令、能力、实际执行值、波块和终态 ABI",
    ),
    (
        "L2 `VnaHardware`",
        "产品 ID/聚合、Trace/Calibration/Result/SCPI/UI 类型",
        "`AcquisitionRecipe`、设备能力、`EffectiveExecutionPlan` 和物理执行事件",
    ),
    (
        "L3 `VnaAcquisition`",
        "Channel 调度、Measurement/Trace/DisplayPane、ResultRef、Repository、SCPI/UI 类型",
        "Prepared execution、WaveSchema、完整 `RawWaveFrame` 与 Acquisition terminal",
    ),
    (
        "L4 数值 Core",
        "`TraceId`、产品 Trace/Measurement/Channel DTO、`ResultRef/ResultArtifact`、Repository、`ProfileAddressBook/ScpiSelectionContext`、线程与文件类型",
        "已准备数值算子、NumericSchema、只读数值视图和不可变数值结果",
    ),
)

EXPECTED_EVIDENCE_PORTABILITY_GAP_MAP = (
    (
        "GAP-SNA-OBJDUMP-REPORT-BUNDLE-001",
        "DEC-EVIDENCE-PORTABILITY-001",
        "离线独立复核、对外发布证据包，以及明确要求可移植证据的放行",
    ),
    (
        "GAP-SNA-SOURCE-PROVENANCE-001",
        "DEC-EVIDENCE-PORTABILITY-001",
        "宣称对象来自官方原包、跨厂商来源归属，以及要求官方来源真实性的放行",
    ),
)

REFERENCE_DIR = Path(__file__).resolve().parent
GFM_INSPECTOR_PATH = REFERENCE_DIR / "verify_mapping_gfm.mjs"
MARKED_ESM_PATH = REFERENCE_DIR / "vendor/marked-17.0.5.esm.js"
MARKED_ESM_SHA256 = "0b4487359ce6b85108708e7a532e1242a27f4718241e7b2de109ac6cd307d2dc"
EXPECTED_NEGATIVE_GUARD_COUNT = 179

STABLE_SEMVER_RE = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
EXACT_REVISION_RE = re.compile(
    r"[A-Za-z][A-Za-z0-9._-]*@sha256:[0-9a-f]{64}"
)

GATE_EVALUATION_KEY_FIELDS = (
    "GateId",
    "AssertionScopeId",
    "AssertionScopeVersion",
    "AssertionScopeDefinitionHash",
    "AssertionScopeSerializationId",
    "CodeRevision",
    "BuildRevision",
    "RuntimeProfileRevision",
    "DeviceProfileRevision",
    "StorageProfileRevision",
    "ProtocolProfileRevision",
    "ExactCapabilityRevision",
    "EvidenceAndTestBundleHash",
)

COMPATIBILITY_GATE_EVALUATION_KEY_FIELDS = (
    "ProductProfileKey",
    "CompatibilityDomain",
    "CompatibilityGateId",
    "TransportProfileRevision",
    "ProtocolProfileRevision",
    "RuntimeProfileRevision",
    "StorageProfileRevision",
    "CompatibilityAssertionScopeId",
    "CompatibilityAssertionScopeVersion",
    "CompatibilityAssertionScopeDefinitionHash",
    "CompatibilityAssertionScopeSerializationId",
    "CodeRevision",
    "BuildRevision",
    "ExactCapabilityRevision",
    "TranscriptAndTestBundleHash",
)

COMPATIBILITY_EVALUATION_RECEIPT_FIELDS = (
    "ReceiptSerializationId",
    "Outcome",
    "EvaluationKeyHash",
    "EvidenceBundleHash",
    "EvaluationDefinitionHash",
    "ProtectedGitRef",
    "CiRunRef",
    "ApprovalRecordRef",
)

ASSERTION_BLOCK_RE = re.compile(
    r"^- \*\*`(?P<id>ASSERT-[A-Z0-9-]+)`\*\*.*?"
    r"(?=^- \*\*`ASSERT-|^#### |^<a id=|\Z)",
    re.MULTILINE | re.DOTALL,
)

COMPATIBILITY_CANDIDATES = {
    "COMPAT-TRIGGER@PROFILE": (
        "Trigger",
        "GATE-COMPAT-TRIGGER-001",
        "ASSERT-COMPAT-TRIGGER-001",
        ("DEC-TRG-MODEL-001", "DEC-TRG-CARD-001"),
    ),
    "COMPAT-SCPI@PROFILE": (
        "SCPI",
        "GATE-COMPAT-SCPI-001",
        "ASSERT-COMPAT-SCPI-001",
        ("DEC-SCPI-ORDER-001",),
    ),
    "COMPAT-DCL-STATUS@PROFILE": (
        "DCL-Status",
        "GATE-COMPAT-DCL-STATUS-001",
        "ASSERT-COMPAT-DCL-STATUS-001",
        ("DEC-SCPI-DCL-STATUS-001", "DEC-STATUS-001"),
    ),
    "COMPAT-RECALL@PROFILE": (
        "Recall",
        "GATE-COMPAT-RECALL-001",
        "ASSERT-COMPAT-RECALL-001",
        ("DEC-PERSIST-001",),
    ),
}

COMPAT_CANDIDATE_ORDER = tuple(COMPATIBILITY_CANDIDATES)

# Draft mode has no released predecessor. The Markdown history table must remain
# visibly empty, and every history row is rejected. A future released verifier
# must add trust-chain verification before it can accept a real replacement edge.
PENDING_COMPAT_PROFILE_STATES = (
    (
        "ZNB/{model}/{firmware}/{options}",
        "Trigger",
        "GATE-COMPAT-TRIGGER-001",
        "BlockedPendingEvidence",
        "SRC-ZNB-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-ZNB-TRIGGER-TRANSCRIPT-001",
    ),
    (
        "ZNB/{model}/{firmware}/{options}",
        "SCPI",
        "GATE-COMPAT-SCPI-001",
        "BlockedPendingEvidence",
        "SRC-ZNB-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-ZNB-SCPI-TRANSCRIPT-001",
    ),
    (
        "ZNB/{model}/{firmware}/{options}",
        "DCL-Status",
        "GATE-COMPAT-DCL-STATUS-001",
        "BlockedPendingEvidence",
        "SRC-ZNB-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-ZNB-DCL-STATUS-TRANSCRIPT-001",
    ),
    (
        "ZNB/{model}/{firmware}/{options}",
        "Recall",
        "GATE-COMPAT-RECALL-001",
        "BlockedPendingEvidence",
        "SRC-ZNB-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-ZNB-RECALL-TRANSCRIPT-001",
    ),
    (
        "SNA/{model}/{firmware}/{options}",
        "Trigger",
        "GATE-COMPAT-TRIGGER-001",
        "BlockedPendingEvidence",
        "SRC-SNA-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-SNA-TRIGGER-TRANSCRIPT-001",
    ),
    (
        "SNA/{model}/{firmware}/{options}",
        "SCPI",
        "GATE-COMPAT-SCPI-001",
        "BlockedPendingEvidence",
        "SRC-SNA-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-SNA-SCPI-TRANSCRIPT-001",
    ),
    (
        "SNA/{model}/{firmware}/{options}",
        "DCL-Status",
        "GATE-COMPAT-DCL-STATUS-001",
        "BlockedPendingEvidence",
        "SRC-SNA-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-SNA-DCL-STATUS-TRANSCRIPT-001",
    ),
    (
        "SNA/{model}/{firmware}/{options}",
        "Recall",
        "GATE-COMPAT-RECALL-001",
        "BlockedPendingEvidence",
        "SRC-SNA-TRANSCRIPT-PENDING",
        "NotIssued",
        "NotIssued",
        "GAP-SNA-RECALL-TRANSCRIPT-001",
    ),
)

INTERNAL_CANDIDATE_ORDER = (
    "TYPE-L0-L3@R1",
    "FAIL-DRIVER-FRAME@R1",
    "CHAIN-RUNTIME@R2",
    "RES-BOUNDED@R2",
    "CONC-RUNTIME@R2",
    "DEP-OWNERSHIP@R2",
    "RESULT-COMMIT@R2",
    "TYPE-COND-NET@S1",
    "CHAIN-PRODUCT@S1",
    "PROV-RESULT@S1",
    "TRG-SINGLE@S1",
    "TRG-CONTINUOUS@S2",
    "NUM-CAL@S3",
    "NUM-MATRIX@S4",
    "TRG-GROUP-GRANULARITY@S5",
    "NUM-XFORM@S6",
    "SCPI-NATIVE@S7",
    "PERSIST-SESSION@S7",
    "FILE-RECOVERY@S7",
)

# Each row is GateId, ScopeId, AssertionId, Decisions, blocking internal GAPs.
# Decision/GAP order is editorial; membership is the candidate semantic boundary.
EXPECTED_GATE_DECISION_GAP = (
    (
        "ARCH-TYPE-01",
        "TYPE-L0-L3@R1",
        "ASSERT-TYPE-L0-L3-001",
        ("DEC-ACQ-PARTIAL-001", "DEC-NUM-SCHEMA-001", "DEC-NUM-SPAN-001"),
        (
            "GAP-ACQ-PARTIAL-INTERNAL-001",
            "GAP-NUM-SCHEMA-INTERNAL-001",
            "GAP-NUM-SPAN-INTERNAL-001",
        ),
    ),
    (
        "ARCH-TYPE-01",
        "TYPE-COND-NET@S1",
        "ASSERT-TYPE-COND-NET-001",
        ("DEC-NUM-COND-001", "DEC-NUM-NETMODEL-001"),
        ("GAP-NUM-COND-INTERNAL-001", "GAP-NUM-NETMODEL-001"),
    ),
    (
        "ARCH-FAIL-01",
        "FAIL-DRIVER-FRAME@R1",
        "ASSERT-FAIL-DRIVER-FRAME-001",
        ("DEC-DRIVER-ISOLATION-001", "DEC-ACQ-COMPLETE-001"),
        (
            "GAP-DRIVER-BLOCKING-INTERNAL-001",
            "GAP-ACQ-COMPLETE-INTERNAL-001",
        ),
    ),
    (
        "ARCH-CHAIN-01",
        "CHAIN-RUNTIME@R2",
        "ASSERT-CHAIN-RUNTIME-001",
        ("DEC-ACQ-COMPLETE-001", "DEC-RUN-ORTHO-001"),
        ("GAP-ACQ-COMPLETE-INTERNAL-001", "GAP-RUN-INTERNAL-001"),
    ),
    (
        "ARCH-CHAIN-01",
        "CHAIN-PRODUCT@S1",
        "ASSERT-CHAIN-PRODUCT-001",
        ("DEC-OBJ-001", "DEC-OBJ-MEAS-001", "DEC-RESULT-PROV-001"),
        (
            "GAP-OBJ-INTERNAL-001",
            "GAP-ANALYSIS-INTERNAL-001",
            "GAP-OBJ-MEAS-INTERNAL-001",
            "GAP-RESULT-PROV-INTERNAL-001",
        ),
    ),
    (
        "ARCH-RES-01",
        "RES-BOUNDED@R2",
        "ASSERT-RES-BOUNDED-001",
        ("DEC-RES-BOUND-001",),
        ("GAP-RES-INTERNAL-001",),
    ),
    (
        "ARCH-CONC-01",
        "CONC-RUNTIME@R2",
        "ASSERT-CONC-RUNTIME-001",
        ("DEC-DEPLOY-CONC-001",),
        ("GAP-DEPLOY-CONC-INTERNAL-001",),
    ),
    (
        "ARCH-DEP-01",
        "DEP-OWNERSHIP@R2",
        "ASSERT-DEP-OWNERSHIP-001",
        ("DEC-LAYER-PURECORE-001", "DEC-DEPLOY-CONC-001"),
        ("GAP-LAYER-INTERNAL-001", "GAP-DEPLOY-CONC-INTERNAL-001"),
    ),
    (
        "ARCH-RESULT-01",
        "RESULT-COMMIT@R2",
        "ASSERT-RESULT-COMMIT-001",
        ("DEC-RESULT-RESP-001", "DEC-RUN-ORTHO-001", "DEC-RES-BOUND-001"),
        (
            "GAP-RESULT-RESP-INTERNAL-001",
            "GAP-RUN-INTERNAL-001",
            "GAP-RES-INTERNAL-001",
        ),
    ),
    (
        "ARCH-PROV-01",
        "PROV-RESULT@S1",
        "ASSERT-PROV-RESULT-001",
        ("DEC-RESULT-PROV-001", "DEC-NUM-SCHEMA-001", "DEC-DATA-ACCESS-001"),
        (
            "GAP-RESULT-PROV-INTERNAL-001",
            "GAP-NUM-SCHEMA-INTERNAL-001",
            "GAP-DATA-ACCESS-INTERNAL-001",
        ),
    ),
    (
        "ARCH-TRG-01",
        "TRG-SINGLE@S1",
        "ASSERT-TRG-ROUTE-SINGLE-001",
        ("DEC-TRG-MODEL-001", "DEC-TRG-CLOCK-001"),
        ("GAP-TRG-INTERNAL-001", "GAP-TRG-CLOCK-001"),
    ),
    (
        "ARCH-TRG-02",
        "TRG-SINGLE@S1",
        "ASSERT-TRG-ADMISSION-SINGLE-001",
        (
            "DEC-TRG-CARD-001",
            "DEC-RUN-SINGLE-LIVE-001",
            "DEC-RES-BOUND-001",
            "DEC-RUN-ORTHO-001",
        ),
        (
            "GAP-TRG-INTERNAL-001",
            "GAP-RUN-INTERNAL-001",
            "GAP-RUN-SINGLE-LIVE-INTERNAL-001",
            "GAP-RES-INTERNAL-001",
        ),
    ),
    (
        "ARCH-TRG-03",
        "TRG-SINGLE@S1",
        "ASSERT-TRG-PROGRESS-SINGLE-001",
        ("DEC-TRG-CARD-001", "DEC-TRG-MODEL-001"),
        ("GAP-TRG-INTERNAL-001",),
    ),
    (
        "ARCH-TRG-04",
        "TRG-SINGLE@S1",
        "ASSERT-TRG-CLOSE-SINGLE-001",
        ("DEC-TRG-MODEL-001", "DEC-RUN-SINGLE-LIVE-001", "DEC-RUN-ORTHO-001"),
        (
            "GAP-TRG-INTERNAL-001",
            "GAP-RUN-INTERNAL-001",
            "GAP-RUN-SINGLE-LIVE-INTERNAL-001",
        ),
    ),
    (
        "ARCH-TRG-01",
        "TRG-CONTINUOUS@S2",
        "ASSERT-TRG-ROUTE-CONTINUOUS-001",
        ("DEC-TRG-MODEL-001", "DEC-TRG-CLOCK-001"),
        ("GAP-TRG-INTERNAL-001", "GAP-TRG-CLOCK-001"),
    ),
    (
        "ARCH-TRG-02",
        "TRG-CONTINUOUS@S2",
        "ASSERT-TRG-ADMISSION-CONTINUOUS-001",
        (
            "DEC-TRG-CARD-001",
            "DEC-RUN-SINGLE-LIVE-001",
            "DEC-RES-BOUND-001",
            "DEC-RUN-ORTHO-001",
        ),
        (
            "GAP-TRG-INTERNAL-001",
            "GAP-RUN-INTERNAL-001",
            "GAP-RUN-SINGLE-LIVE-INTERNAL-001",
            "GAP-RES-INTERNAL-001",
        ),
    ),
    (
        "ARCH-TRG-03",
        "TRG-CONTINUOUS@S2",
        "ASSERT-TRG-PROGRESS-CONTINUOUS-001",
        ("DEC-TRG-CARD-001", "DEC-TRG-MODEL-001"),
        ("GAP-TRG-INTERNAL-001",),
    ),
    (
        "ARCH-TRG-04",
        "TRG-CONTINUOUS@S2",
        "ASSERT-TRG-CLOSE-CONTINUOUS-001",
        ("DEC-TRG-MODEL-001", "DEC-RUN-SINGLE-LIVE-001", "DEC-RUN-ORTHO-001"),
        (
            "GAP-TRG-INTERNAL-001",
            "GAP-RUN-INTERNAL-001",
            "GAP-RUN-SINGLE-LIVE-INTERNAL-001",
        ),
    ),
    (
        "ARCH-TRG-01",
        "TRG-GROUP-GRANULARITY@S5",
        "ASSERT-TRG-ROUTE-GROUP-001",
        ("DEC-TRG-MODEL-001", "DEC-TRG-CLOCK-001"),
        ("GAP-TRG-INTERNAL-001", "GAP-TRG-CLOCK-001"),
    ),
    (
        "ARCH-TRG-02",
        "TRG-GROUP-GRANULARITY@S5",
        "ASSERT-TRG-ADMISSION-GROUP-001",
        (
            "DEC-TRG-CARD-001",
            "DEC-RUN-SINGLE-LIVE-001",
            "DEC-RES-BOUND-001",
            "DEC-RUN-ORTHO-001",
        ),
        (
            "GAP-TRG-INTERNAL-001",
            "GAP-RUN-INTERNAL-001",
            "GAP-RUN-SINGLE-LIVE-INTERNAL-001",
            "GAP-RES-INTERNAL-001",
        ),
    ),
    (
        "ARCH-TRG-03",
        "TRG-GROUP-GRANULARITY@S5",
        "ASSERT-TRG-PROGRESS-GROUP-001",
        ("DEC-TRG-CARD-001", "DEC-ACQ-PARTIAL-001", "DEC-TRG-MODEL-001"),
        ("GAP-TRG-INTERNAL-001", "GAP-ACQ-PARTIAL-INTERNAL-001"),
    ),
    (
        "ARCH-TRG-04",
        "TRG-GROUP-GRANULARITY@S5",
        "ASSERT-TRG-CLOSE-GROUP-001",
        ("DEC-TRG-MODEL-001", "DEC-RUN-SINGLE-LIVE-001", "DEC-RUN-ORTHO-001"),
        (
            "GAP-TRG-INTERNAL-001",
            "GAP-RUN-INTERNAL-001",
            "GAP-RUN-SINGLE-LIVE-INTERNAL-001",
        ),
    ),
    (
        "ARCH-NUM-01",
        "NUM-CAL@S3",
        "ASSERT-NUM-CAL-001",
        (
            "DEC-CAL-OWN-001",
            "DEC-CAL-COMP-001",
            "DEC-CAL-PUBLISH-001",
            "DEC-CAL-NUM-001",
            "DEC-CAL-STD-MODEL-001",
        ),
        (
            "GAP-CAL-OWN-INTERNAL-001",
            "GAP-CAL-COMP-INTERNAL-001",
            "GAP-CAL-PUBLISH-INTERNAL-001",
            "GAP-CAL-MATH-001",
            "GAP-CAL-STD-MODEL-001",
        ),
    ),
    (
        "ARCH-NUM-01",
        "NUM-MATRIX@S4",
        "ASSERT-NUM-MATRIX-001",
        ("DEC-NUM-NETMODEL-001", "DEC-ACQ-PARTIAL-001"),
        ("GAP-NUM-NETMODEL-001", "GAP-ACQ-PARTIAL-INTERNAL-001"),
    ),
    (
        "ARCH-NUM-01",
        "NUM-XFORM@S6",
        "ASSERT-NUM-XFORM-001",
        ("DEC-XFORM-ORDER-001",),
        ("GAP-XFORM-ORDER-INTERNAL-001",),
    ),
    (
        "ARCH-SCPI-01",
        "SCPI-NATIVE@S7",
        "ASSERT-SCPI-NATIVE-001",
        ("DEC-SCPI-ORDER-001", "DEC-SCPI-DCL-STATUS-001", "DEC-STATUS-001"),
        (
            "GAP-SCPI-INTERNAL-001",
            "GAP-SCPI-DCL-STATUS-001",
            "GAP-STATUS-INTERNAL-001",
        ),
    ),
    (
        "ARCH-PER-01",
        "PERSIST-SESSION@S7",
        "ASSERT-PERSIST-SESSION-001",
        ("DEC-PERSIST-001",),
        ("GAP-PERSIST-RUNTIME-001",),
    ),
    (
        "ARCH-FILE-01",
        "FILE-RECOVERY@S7",
        "ASSERT-FILE-RECOVERY-001",
        ("DEC-PERSIST-001", "DEC-DRIVER-ISOLATION-001"),
        ("GAP-PERSIST-RUNTIME-001", "GAP-DRIVER-BLOCKING-INTERNAL-001"),
    ),
)

class VerificationError(RuntimeError):
    """Raised for malformed manifests or mismatched hashes."""


@dataclass(frozen=True)
class ScopeResult:
    family: str
    serialization_id: str
    definition_source: str
    activation: str
    scope_id: str
    version: str
    assertion_ids: tuple[str, ...]
    stored_sha256: str
    computed_sha256: str

    @property
    def definition_integrity(self) -> bool:
        return self.stored_sha256 == self.computed_sha256


def read_utf8_lf(path: Path) -> str:
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raise VerificationError(f"{path}: UTF-8 BOM is not canonical")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise VerificationError(f"{path}: invalid UTF-8: {exc}") from exc
    return text.replace("\r\n", "\n").replace("\r", "\n")


def one_code_value(cell: str, label: str) -> str:
    match = re.fullmatch(r"`([^`]+)`", cell)
    if not match:
        raise VerificationError(f"invalid {label}: {cell!r}")
    return match.group(1)


def stable_semver(value: str, label: str) -> tuple[int, int, int]:
    """Parse the registry's stable SemVer subset without leading zeroes."""

    if not STABLE_SEMVER_RE.fullmatch(value):
        raise VerificationError(
            f"invalid {label}: {value!r}; expected stable SemVer MAJOR.MINOR.PATCH"
        )
    return tuple(int(component) for component in value.split("."))


def require_exact_declaration(
    text: str,
    pattern: str,
    expected: str,
    label: str,
    source: Path,
    gfm: dict[str, object],
) -> int:
    values = re.findall(pattern, text)
    if values != [expected]:
        raise VerificationError(
            f"{source}: {label} must be declared exactly once as {expected!r}, got {values!r}"
        )
    top_matches = [
        index
        for index, token in enumerate(gfm_records(gfm, "topLevel", source))
        if token.get("type") == "paragraph"
        and isinstance(token.get("raw"), str)
        and re.findall(pattern, str(token["raw"])) == [expected]
    ]
    if len(top_matches) != 1:
        raise VerificationError(
            f"{source}: {label} declaration must be one active top-level GFM paragraph"
        )
    return top_matches[0]


def verify_exact_key_block(
    text: str, key_name: str, expected_fields: tuple[str, ...], source: Path
) -> None:
    expected_lines = [f"  {field}," for field in expected_fields[:-1]]
    expected_lines.append(f"  {expected_fields[-1]}")
    expected_text = "\n".join(
        (f"{key_name} = {{", *expected_lines, "}")
    )
    expected_raw = f"```text\n{expected_text}\n```"
    gfm = verified_gfm_document(text, source)
    global_matches = [
        token
        for token in gfm_records(gfm, "codes", source)
        if token.get("text") == expected_text
    ]
    top_matches = [
        token
        for token in gfm_records(gfm, "topLevel", source)
        if token.get("type") == "code" and token.get("text") == expected_text
    ]
    if (
        len(global_matches) != 1
        or len(top_matches) != 1
        or global_matches[0].get("lang") != "text"
        or global_matches[0].get("raw") != expected_raw
        or top_matches[0].get("lang") != "text"
        or top_matches[0].get("raw") != expected_raw
    ):
        raise VerificationError(
            f"{source}: {key_name} must be one exact active top-level GFM text block"
        )


def code_id_list(cell: str, prefix: str, label: str) -> tuple[str, ...]:
    ids = tuple(re.findall(rf"`({re.escape(prefix)}-[A-Z0-9-]+)`", cell))
    canonical = "、".join(f"`{item}`" for item in ids)
    if not ids or cell != canonical:
        raise VerificationError(f"invalid {label}: {cell!r}")
    if len(ids) != len(set(ids)):
        raise VerificationError(f"duplicate {label}: {ids!r}")
    return ids


def verify_gate_decision_gap_matrix(
    mapping_text: str,
    mapping_path: Path,
    current_scopes: list[ScopeResult],
) -> None:
    gfm = verified_gfm_document(mapping_text, mapping_path)
    start_index = require_top_level_anchor(
        gfm,
        GATE_DECISION_GAP_START,
        "Gate/Decision/GAP start anchor",
        mapping_path,
    )
    section_heading_index = require_top_level_heading(
        gfm,
        "## 验收门到决策与内部缺口的唯一映射",
        2,
        "验收门到决策与内部缺口的唯一映射",
        "Gate/Decision/GAP heading",
        mapping_path,
    )
    end_index = require_top_level_anchor(
        gfm,
        GATE_DECISION_GAP_END,
        "Decision reverse-index anchor",
        mapping_path,
    )
    reverse_heading_index = require_top_level_heading(
        gfm,
        "## 决策反向索引",
        2,
        "决策反向索引",
        "Decision reverse-index heading",
        mapping_path,
    )
    rows, _table, table_index = require_top_level_gfm_table(
        gfm,
        ("Gate / Scope", "AssertionId", "关键 Decision", "阻断性内部 GAP"),
        "Gate/Decision/GAP matrix",
        mapping_path,
        start_index,
        end_index,
    )
    if not (
        start_index < section_heading_index < table_index < end_index
        < reverse_heading_index
    ):
        raise VerificationError(
            f"{mapping_path}: Gate/Decision/GAP controls are out of canonical order"
        )

    actual: dict[
        tuple[str, str], tuple[str, tuple[str, ...], tuple[str, ...]]
    ] = {}
    actual_order: list[tuple[str, str]] = []
    for cells in rows:
        gate_scope = one_code_value(cells[0], "Gate / Scope")
        match = re.fullmatch(
            r"(ARCH-[A-Z0-9-]+) / ([A-Z0-9-]+@[A-Z0-9]+)", gate_scope
        )
        if not match:
            raise VerificationError(f"invalid Gate / Scope: {gate_scope!r}")
        key = (match.group(1), match.group(2))
        if key in actual:
            raise VerificationError(f"duplicate Gate / Scope mapping: {key!r}")
        assertion_id = one_code_value(cells[1], "mapping AssertionId")
        if not re.fullmatch(r"ASSERT-[A-Z0-9-]+", assertion_id):
            raise VerificationError(f"invalid mapping AssertionId: {assertion_id!r}")
        decisions = code_id_list(cells[2], "DEC", "DecisionId")
        gaps = code_id_list(cells[3], "GAP", "GAPId")
        actual[key] = (assertion_id, decisions, gaps)
        actual_order.append(key)

    expected = {
        (gate, scope): (assertion, decisions, gaps)
        for gate, scope, assertion, decisions, gaps in EXPECTED_GATE_DECISION_GAP
    }
    expected_order = [
        (gate, scope) for gate, scope, _, _, _ in EXPECTED_GATE_DECISION_GAP
    ]
    if len(expected) != 28 or len(EXPECTED_GATE_DECISION_GAP) != 28:
        raise VerificationError("verifier fixture must contain 28 unique mappings")
    if actual_order != expected_order:
        raise VerificationError(
            f"{mapping_path}: Gate/Decision/GAP rows are missing, duplicated, or reordered"
        )

    for key, expected_value in expected.items():
        actual_value = actual[key]
        expected_assertion, expected_decisions, expected_gaps = expected_value
        actual_assertion, actual_decisions, actual_gaps = actual_value
        if actual_assertion != expected_assertion:
            raise VerificationError(
                f"{mapping_path}: {key!r} AssertionId must be "
                f"{expected_assertion!r}, got {actual_assertion!r}"
            )
        if set(actual_decisions) != set(expected_decisions):
            raise VerificationError(
                f"{mapping_path}: {key!r} Decisions must be "
                f"{expected_decisions!r}, got {actual_decisions!r}"
            )
        if set(actual_gaps) != set(expected_gaps):
            raise VerificationError(
                f"{mapping_path}: {key!r} GAPs must be "
                f"{expected_gaps!r}, got {actual_gaps!r}"
            )

    registered = {
        (item.scope_id, assertion_id)
        for item in current_scopes
        for assertion_id in item.assertion_ids
    }
    mapped = {
        (scope_id, value[0]) for (_, scope_id), value in actual.items()
    }
    if mapped != registered:
        raise VerificationError(
            f"{mapping_path}: matrix Scope/Assertion pairs do not match current registry"
        )

    for decision_id in sorted(
        {decision for value in actual.values() for decision in value[1]}
    ):
        decision_index = require_top_level_heading(
            gfm,
            f"### `{decision_id}`",
            3,
            decision_id,
            f"{decision_id} definition heading",
            mapping_path,
        )
        if decision_index <= reverse_heading_index:
            raise VerificationError(
                f"{mapping_path}: {decision_id} must remain in the Decision reverse index"
            )


def inspect_gfm_document(document_text: str, document_path: Path) -> dict[str, object]:
    """Parse one protected Markdown document with the pinned GFM lexer."""

    try:
        marked_bytes = MARKED_ESM_PATH.read_bytes()
    except OSError as exc:
        raise VerificationError(
            f"{document_path}: pinned GFM parser is unavailable: {exc}"
        ) from exc
    actual_marked_hash = hashlib.sha256(marked_bytes).hexdigest()
    if actual_marked_hash != MARKED_ESM_SHA256:
        raise VerificationError(
            f"{document_path}: pinned marked@17.0.5 hash mismatch: "
            f"{actual_marked_hash}"
        )
    if not GFM_INSPECTOR_PATH.is_file():
        raise VerificationError(
            f"{document_path}: missing GFM inspector {GFM_INSPECTOR_PATH}"
        )
    node = shutil.which("node")
    if node is None:
        raise VerificationError(
            f"{document_path}: Node.js is required for deterministic GFM verification"
        )
    try:
        completed = subprocess.run(
            [node, str(GFM_INSPECTOR_PATH)],
            input=document_text,
            text=True,
            encoding="utf-8",
            errors="strict",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=20,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired, UnicodeError) as exc:
        raise VerificationError(
            f"{document_path}: GFM inspector could not complete: {exc}"
        ) from exc
    if completed.returncode != 0:
        raise VerificationError(
            f"{document_path}: GFM inspector failed: {completed.stderr.strip()}"
        )
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise VerificationError(
            f"{document_path}: GFM inspector emitted invalid JSON"
        ) from exc
    if (
        not isinstance(result, dict)
        or result.get("parser") != "marked@17.0.5"
        or not isinstance(result.get("headings"), list)
        or not isinstance(result.get("tables"), list)
        or not isinstance(result.get("html"), list)
        or not isinstance(result.get("codes"), list)
        or not isinstance(result.get("assertionItems"), list)
        or not isinstance(result.get("topLevelAssertionItems"), list)
        or not isinstance(result.get("topLevel"), list)
    ):
        raise VerificationError(
            f"{document_path}: GFM inspector result has an unsupported schema"
        )
    top_level = result["topLevel"]
    if any(
        not isinstance(token, dict) or not isinstance(token.get("raw"), str)
        for token in top_level
    ):
        raise VerificationError(
            f"{document_path}: GFM top-level token stream has an invalid schema"
        )
    if "".join(str(token["raw"]) for token in top_level) != document_text:
        raise VerificationError(
            f"{document_path}: GFM top-level token stream does not cover the exact input"
        )
    return result


def normalize_gfm_text(value: object, label: str, source: Path) -> str:
    if not isinstance(value, str):
        raise VerificationError(f"{source}: {label} must be text")
    return re.sub(r"\s+", " ", html.unescape(value)).strip()


def gfm_records(
    gfm: dict[str, object], key: str, source: Path
) -> list[dict[str, object]]:
    value = gfm.get(key)
    if not isinstance(value, list) or any(not isinstance(item, dict) for item in value):
        raise VerificationError(f"{source}: GFM {key} has an invalid schema")
    return value


def valid_html_comment(raw: str) -> bool:
    """Accept one HTML comment only when HTML5 cannot close it early."""

    if not (raw.startswith("<!--") and raw.endswith("-->")):
        return False
    body = raw[4:-3]
    return not (
        body.startswith((">", "->"))
        or "<!--" in body
        or "-->" in body
        or "--!>" in body
        or body.endswith("<!-")
    )


def verify_gfm_html_surface(gfm: dict[str, object], source: Path) -> None:
    """Allow only valid comments and unique top-level empty anchors."""

    html_tokens = gfm.get("html")
    if not isinstance(html_tokens, list):
        raise VerificationError(f"{source}: GFM html has an invalid schema")
    anchor_open_ids: list[str] = []
    anchor_close_count = 0
    for index, raw_html in enumerate(html_tokens):
        if not isinstance(raw_html, str):
            raise VerificationError(f"{source}: GFM html[{index}] must be text")
        stripped = raw_html.strip()
        anchor_open = re.fullmatch(r'<a id="([a-z0-9-]+)">', stripped)
        if valid_html_comment(stripped):
            continue
        if anchor_open is not None:
            anchor_open_ids.append(anchor_open.group(1))
            continue
        if stripped == "</a>":
            anchor_close_count += 1
            continue
        raise VerificationError(
            f"{source}: raw HTML is forbidden outside canonical comments/anchors: "
            f"html[{index}]={stripped!r}"
        )

    top_level_anchor_ids: list[str] = []
    for index, token in enumerate(gfm_records(gfm, "topLevel", source)):
        token_type = token.get("type")
        token_raw = token.get("raw")
        if not isinstance(token_type, str) or not isinstance(token_raw, str):
            raise VerificationError(
                f"{source}: GFM topLevel[{index}] has an invalid schema"
            )
        if token_type != "paragraph":
            continue
        anchor = re.fullmatch(r'<a id="([a-z0-9-]+)"></a>', token_raw)
        if anchor is not None:
            top_level_anchor_ids.append(anchor.group(1))

    if (
        sorted(anchor_open_ids) != sorted(top_level_anchor_ids)
        or anchor_close_count != len(top_level_anchor_ids)
        or len(set(top_level_anchor_ids)) != len(top_level_anchor_ids)
    ):
        raise VerificationError(
            f"{source}: every active HTML anchor must be one unique, top-level "
            "canonical empty anchor"
        )


def verified_gfm_document(text: str, source: Path) -> dict[str, object]:
    gfm = inspect_gfm_document(text, source)
    verify_gfm_html_surface(gfm, source)
    return gfm


def require_top_level_anchor(
    gfm: dict[str, object], raw: str, label: str, source: Path
) -> int:
    matches = [
        index
        for index, token in enumerate(gfm_records(gfm, "topLevel", source))
        if token.get("type") == "paragraph" and token.get("raw") == raw
    ]
    if len(matches) != 1:
        raise VerificationError(
            f"{source}: {label} must be one exact top-level GFM anchor"
        )
    return matches[0]


def require_top_level_comment(
    gfm: dict[str, object], raw: str, label: str, source: Path
) -> int:
    global_matches = [
        item
        for item in gfm.get("html", [])
        if isinstance(item, str) and item.rstrip("\n") == raw
    ]
    top_matches = [
        index
        for index, token in enumerate(gfm_records(gfm, "topLevel", source))
        if token.get("type") == "html"
        and isinstance(token.get("raw"), str)
        and str(token["raw"]).rstrip("\n") == raw
    ]
    if len(global_matches) != 1 or len(top_matches) != 1:
        raise VerificationError(
            f"{source}: {label} must be one exact top-level GFM comment"
        )
    return top_matches[0]


def require_top_level_heading(
    gfm: dict[str, object], raw: str, depth: int, text: str, label: str, source: Path
) -> int:
    semantic_matches: list[dict[str, object]] = []
    for index, heading in enumerate(gfm_records(gfm, "headings", source)):
        actual_text = normalize_gfm_text(
            heading.get("text"), f"GFM heading[{index}].text", source
        )
        if actual_text == text:
            semantic_matches.append(heading)
    top_matches = [
        index
        for index, token in enumerate(gfm_records(gfm, "topLevel", source))
        if token.get("type") == "heading"
        and token.get("depth") == depth
        and isinstance(token.get("raw"), str)
        and str(token["raw"]).rstrip("\n") == raw
        and normalize_gfm_text(token.get("text"), f"GFM topLevel[{index}].text", source)
        == text
    ]
    if (
        len(semantic_matches) != 1
        or semantic_matches[0].get("depth") != depth
        or not isinstance(semantic_matches[0].get("raw"), str)
        or str(semantic_matches[0]["raw"]).rstrip("\n") != raw
        or len(top_matches) != 1
    ):
        raise VerificationError(
            f"{source}: {label} must be one exact, semantic top-level GFM heading"
        )
    return top_matches[0]


def require_top_level_gfm_table(
    gfm: dict[str, object],
    expected_header: tuple[str, ...],
    label: str,
    source: Path,
    start_index: int,
    end_index: int,
    *,
    only_structure_between: bool = False,
) -> tuple[list[list[str]], dict[str, object], int]:
    """Return canonical source cells from one visible top-level GFM table."""

    def semantic_header(table: dict[str, object], table_label: str) -> tuple[str, ...]:
        header = table.get("header")
        if not isinstance(header, list):
            raise VerificationError(f"{source}: {table_label}.header has invalid schema")
        return tuple(
            normalize_gfm_text(cell, f"{table_label}.header[{index}]", source)
            for index, cell in enumerate(header)
        )

    global_matches: list[dict[str, object]] = []
    for index, table in enumerate(gfm_records(gfm, "tables", source)):
        if semantic_header(table, f"GFM table[{index}]") == expected_header:
            global_matches.append(table)

    top_matches: list[tuple[int, dict[str, object]]] = []
    for index, token in enumerate(gfm_records(gfm, "topLevel", source)):
        if token.get("type") != "table":
            continue
        if semantic_header(token, f"GFM topLevel[{index}]") == expected_header:
            top_matches.append((index, token))

    if len(global_matches) != 1 or len(top_matches) != 1:
        raise VerificationError(
            f"{source}: {label} must be exactly one active top-level GFM table"
        )
    table_index, table = top_matches[0]
    if not (start_index < table_index < end_index):
        raise VerificationError(
            f"{source}: {label} must remain inside its active top-level boundaries"
        )

    header_source = table.get("headerSource")
    rows_source = table.get("rowsSource")
    raw = table.get("raw")
    if (
        not isinstance(header_source, list)
        or any(not isinstance(cell, str) for cell in header_source)
        or not isinstance(rows_source, list)
        or any(
            not isinstance(row, list)
            or any(not isinstance(cell, str) for cell in row)
            for row in rows_source
        )
        or not isinstance(raw, str)
    ):
        raise VerificationError(f"{source}: {label} has an invalid GFM table schema")
    if tuple(header_source) != expected_header:
        raise VerificationError(f"{source}: {label} header source is not canonical")
    if any(len(row) != len(expected_header) for row in rows_source):
        raise VerificationError(
            f"{source}: every {label} row must have {len(expected_header)} cells"
        )

    header_line = "| " + " | ".join(expected_header) + " |"
    separator_line = "| " + " | ".join("---" for _ in expected_header) + " |"
    canonical_raw = "\n".join(
        (
            header_line,
            separator_line,
            *("| " + " | ".join(row) + " |" for row in rows_source),
        )
    )
    if raw.rstrip("\n") != canonical_raw or global_matches[0].get("raw") != raw:
        raise VerificationError(f"{source}: {label} source representation is not canonical")

    if only_structure_between:
        nonspace = [
            index
            for index, token in enumerate(gfm_records(gfm, "topLevel", source))
            if start_index < index < end_index and token.get("type") != "space"
        ]
        if nonspace != [table_index]:
            raise VerificationError(
                f"{source}: {label} boundaries may contain only their exact table"
            )
    return rows_source, table, table_index


def verify_evidence_portability_gap_map(
    mapping_text: str, mapping_path: Path
) -> None:
    """Verify the non-product SNA evidence-governance backlink."""

    gfm = verified_gfm_document(mapping_text, mapping_path)
    start_index = require_top_level_comment(
        gfm,
        EVIDENCE_PORTABILITY_GAP_MAP_START,
        "evidence portability GAP map start marker",
        mapping_path,
    )
    end_index = require_top_level_comment(
        gfm,
        EVIDENCE_PORTABILITY_GAP_MAP_END,
        "evidence portability GAP map end marker",
        mapping_path,
    )
    rows, _table, _table_index = require_top_level_gfm_table(
        gfm,
        ("Evidence portability GAP", "治理 Decision", "只阻断什么"),
        "evidence portability GAP map",
        mapping_path,
        start_index,
        end_index,
        only_structure_between=True,
    )
    if len(rows) != len(EXPECTED_EVIDENCE_PORTABILITY_GAP_MAP):
        raise VerificationError(
            f"{mapping_path}: evidence governance GAP map must contain exactly "
            f"{len(EXPECTED_EVIDENCE_PORTABILITY_GAP_MAP)} rows"
        )

    actual = tuple(
        (
            one_code_value(row[0], "evidence governance GAP"),
            one_code_value(row[1], "evidence governance Decision"),
            row[2],
        )
        for row in rows
    )
    if actual != EXPECTED_EVIDENCE_PORTABILITY_GAP_MAP:
        raise VerificationError(
            f"{mapping_path}: evidence governance GAP map differs from the "
            f"candidate governance mapping: {actual!r}"
        )

    semantic_gap_rows: list[tuple[str, ...]] = []
    expected_gaps = {item[0] for item in EXPECTED_EVIDENCE_PORTABILITY_GAP_MAP}
    for table_index, table in enumerate(gfm_records(gfm, "tables", mapping_path)):
        table_rows = table.get("rows")
        if not isinstance(table_rows, list):
            raise VerificationError(
                f"{mapping_path}: GFM table[{table_index}].rows has an invalid schema"
            )
        for row_index, row in enumerate(table_rows):
            if not isinstance(row, list):
                raise VerificationError(
                    f"{mapping_path}: GFM table[{table_index}].rows[{row_index}] "
                    "has an invalid schema"
                )
            normalized_row = tuple(
                normalize_gfm_text(
                    cell,
                    f"GFM table[{table_index}].rows[{row_index}][{cell_index}]",
                    mapping_path,
                )
                for cell_index, cell in enumerate(row)
            )
            if normalized_row and normalized_row[0] in expected_gaps:
                semantic_gap_rows.append(normalized_row)
    if tuple(semantic_gap_rows) != EXPECTED_EVIDENCE_PORTABILITY_GAP_MAP:
        raise VerificationError(
            f"{mapping_path}: rendered GFM must contain exactly the candidate "
            "evidence governance GAP rows"
        )

    decision_anchor = '<a id="dec-evidence-portability-001"></a>'
    decision_id = "DEC-EVIDENCE-PORTABILITY-001"
    anchor_index = require_top_level_anchor(
        gfm,
        decision_anchor,
        "evidence portability Decision anchor",
        mapping_path,
    )
    heading_index = require_top_level_heading(
        gfm,
        f"### `{decision_id}`",
        3,
        decision_id,
        "evidence portability Decision heading",
        mapping_path,
    )
    top_level = gfm_records(gfm, "topLevel", mapping_path)
    if (
        heading_index != anchor_index + 2
        or anchor_index + 1 >= len(top_level)
        or top_level[anchor_index + 1].get("type") != "space"
        or top_level[anchor_index + 1].get("raw") != "\n\n"
    ):
        raise VerificationError(
            f"{mapping_path}: Decision heading must immediately follow its "
            "top-level anchor with exactly one blank line"
        )


def activation_from_scope(scope_id: str) -> str:
    try:
        activation = scope_id.rsplit("@", 1)[1]
    except IndexError as exc:
        raise VerificationError(f"{scope_id}: missing activation suffix") from exc
    if not re.fullmatch(r"(?:R|S)[0-9]+|PROFILE", activation):
        raise VerificationError(f"{scope_id}: invalid activation suffix {activation!r}")
    return activation


def sha256_hex(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8", errors="strict")).hexdigest()


def internal_payload(scope_id: str, version: str, blocks: tuple[str, ...]) -> str:
    components = (scope_id, version, *blocks)
    for component in components:
        if INTERNAL_SEPARATOR in component:
            raise VerificationError(
                f"{scope_id}: canonical component contains reserved separator"
            )
    return INTERNAL_SEPARATOR.join(components)


def parse_assertion_blocks(
    d5_text: str, gfm: dict[str, object], d5_path: Path
) -> dict[str, str]:
    def assertion_records(key: str) -> list[tuple[str, str]]:
        records: list[tuple[str, str]] = []
        for index, item in enumerate(gfm_records(gfm, key, d5_path)):
            assertion_id = item.get("id")
            raw = item.get("raw")
            if not isinstance(assertion_id, str) or not isinstance(raw, str):
                raise VerificationError(
                    f"{d5_path}: GFM {key}[{index}] has an invalid schema"
                )
            definition_raw = raw.rstrip("\n")
            if not definition_raw.startswith(f"- **`{assertion_id}`**"):
                raise VerificationError(
                    f"{d5_path}: {assertion_id} definition source is not canonical"
                )
            records.append((assertion_id, definition_raw))
        return records

    global_records = assertion_records("assertionItems")
    top_records = assertion_records("topLevelAssertionItems")
    if global_records != top_records:
        raise VerificationError(
            f"{d5_path}: every assertion definition must be an active top-level "
            "unordered-list item"
        )
    if len(top_records) != 28 or len({item[0] for item in top_records}) != 28:
        raise VerificationError(
            f"{d5_path}: expected 28 unique active top-level assertions, got "
            f"{len(top_records)}"
        )
    return dict(top_records)


def verify_d5_policy_tables(d5_text: str, d5_path: Path) -> None:
    """Protect the SCPI addressing and static-dependency candidate contracts."""

    gfm = verified_gfm_document(d5_text, d5_path)

    scpi_anchor_index = require_top_level_anchor(
        gfm,
        SCPI_ADDRESS_SELECTION_START,
        "SCPI address-selection anchor",
        d5_path,
    )
    scpi_heading_index = require_top_level_heading(
        gfm,
        "#### 10.1.2 厂商编号、产品对象与连接选择",
        4,
        "10.1.2 厂商编号、产品对象与连接选择",
        "SCPI address-selection heading",
        d5_path,
    )
    scpi_end_index = require_top_level_heading(
        gfm,
        SCPI_ADDRESS_SELECTION_END,
        4,
        "10.1.3 优先控制：设备清除（DCL）与证据边界",
        "SCPI address-selection end heading",
        d5_path,
    )
    address_rows, _address_table, address_table_index = require_top_level_gfm_table(
        gfm,
        ("厂商地址域", "精确产品目标", "额外一致性约束"),
        "SCPI address binding table",
        d5_path,
        scpi_anchor_index,
        scpi_end_index,
    )
    profile_rows, _profile_table, profile_table_index = require_top_level_gfm_table(
        gfm,
        ("Profile 类别", "编号与选择规则的准入证据", "当前状态与允许行为"),
        "SCPI profile-address admission table",
        d5_path,
        scpi_anchor_index,
        scpi_end_index,
    )
    if tuple(map(tuple, address_rows)) != EXPECTED_SCPI_ADDRESS_BINDINGS:
        raise VerificationError(
            f"{d5_path}: SCPI address bindings differ from the candidate contract"
        )
    visible_scpi_text = "\n".join(
        normalize_gfm_text(
            token.get("text"),
            f"GFM topLevel[{index}].text",
            d5_path,
        )
        for index, token in enumerate(gfm_records(gfm, "topLevel", d5_path))
        if scpi_anchor_index < index < scpi_end_index
        and isinstance(token.get("text"), str)
    )
    missing_address_model_markers = tuple(
        marker
        for marker in EXPECTED_SCPI_ADDRESS_MODEL_MARKERS
        if marker not in visible_scpi_text
    )
    if missing_address_model_markers:
        raise VerificationError(
            f"{d5_path}: SCPI address model is missing stable-identity controls: "
            f"{missing_address_model_markers!r}"
        )
    visible_scpi_source = "\n".join(
        str(token["raw"])
        for index, token in enumerate(gfm_records(gfm, "topLevel", d5_path))
        if scpi_anchor_index < index < scpi_end_index
        and token.get("type") not in {"code", "html", "blockquote"}
    )
    missing_visible_source_markers = tuple(
        marker
        for marker in EXPECTED_SCPI_ADDRESS_VISIBLE_SOURCE_MARKERS
        if marker not in visible_scpi_source
    )
    if missing_visible_source_markers:
        raise VerificationError(
            f"{d5_path}: SCPI address model is missing visible topology/ABA controls: "
            f"{missing_visible_source_markers!r}"
        )
    if tuple(map(tuple, profile_rows)) != EXPECTED_SCPI_PROFILE_ADMISSION:
        raise VerificationError(
            f"{d5_path}: SCPI profile admission must keep ZNB/SNA pending"
        )
    if not (
        scpi_anchor_index < scpi_heading_index < address_table_index
        < profile_table_index < scpi_end_index
    ):
        raise VerificationError(
            f"{d5_path}: SCPI address-selection controls are out of canonical order"
        )

    dependency_anchor_index = require_top_level_anchor(
        gfm,
        TARGET_DEPENDENCY_POLICY_START,
        "target dependency-policy anchor",
        d5_path,
    )
    dependency_heading_index = require_top_level_heading(
        gfm,
        "#### 12.1.1 构建目标直接依赖白名单",
        4,
        "12.1.1 构建目标直接依赖白名单",
        "target dependency allowlist heading",
        d5_path,
    )
    deny_heading_index = require_top_level_heading(
        gfm,
        "#### 12.1.2 L0–L4 产品类型禁用表",
        4,
        "12.1.2 L0–L4 产品类型禁用表",
        "lower-layer type denylist heading",
        d5_path,
    )
    ci_heading_index = require_top_level_heading(
        gfm,
        "#### 12.1.3 CI 如何机械检查",
        4,
        "12.1.3 CI 如何机械检查",
        "dependency CI heading",
        d5_path,
    )
    dependency_end_index = require_top_level_heading(
        gfm,
        TARGET_DEPENDENCY_POLICY_END,
        3,
        "12.2 分层测试门禁",
        "target dependency-policy end heading",
        d5_path,
    )
    allow_rows, _allow_table, allow_table_index = require_top_level_gfm_table(
        gfm,
        ("构建目标或目标组", "归属层", "允许直接依赖的内部层/目标"),
        "target dependency allowlist",
        d5_path,
        dependency_anchor_index,
        dependency_end_index,
    )
    deny_rows, _deny_table, deny_table_index = require_top_level_gfm_table(
        gfm,
        ("作用范围", "public header / AST 中禁止的上层类型族", "应使用的边界"),
        "lower-layer product-type denylist",
        d5_path,
        dependency_anchor_index,
        dependency_end_index,
    )
    if tuple(map(tuple, allow_rows)) != EXPECTED_TARGET_DEPENDENCY_ALLOWLIST:
        raise VerificationError(
            f"{d5_path}: target dependency allowlist differs from the candidate policy"
        )
    if tuple(map(tuple, deny_rows)) != EXPECTED_LOWER_LAYER_TYPE_DENYLIST:
        raise VerificationError(
            f"{d5_path}: lower-layer product-type denylist differs from the candidate policy"
        )
    if not (
        dependency_anchor_index < dependency_heading_index < allow_table_index
        < deny_heading_index < deny_table_index < ci_heading_index
        < dependency_end_index
    ):
        raise VerificationError(
            f"{d5_path}: target dependency-policy controls are out of canonical order"
        )


def verify_internal_current(d5_text: str, d5_path: Path) -> list[ScopeResult]:
    gfm = verified_gfm_document(d5_text, d5_path)
    start_index = require_top_level_anchor(
        gfm,
        INTERNAL_REGISTRY_START,
        "internal ScopeRegistry anchor",
        d5_path,
    )
    registry_heading_index = require_top_level_heading(
        gfm,
        INTERNAL_REGISTRY_NATIVE_HEADING,
        5,
        "验收范围规范登记表（ScopeRegistry）",
        "native ScopeRegistry heading",
        d5_path,
    )
    end_index = require_top_level_heading(
        gfm,
        INTERNAL_REGISTRY_END,
        5,
        "厂商兼容评估键",
        "internal ScopeRegistry end heading",
        d5_path,
    )
    history_anchor_index = require_top_level_anchor(
        gfm,
        '<a id="dd-scope-version-history"></a>',
        "internal scope-history anchor",
        d5_path,
    )
    lifecycle_index = require_exact_declaration(
        d5_text,
        r"当前注册表生命周期是 `([^`]+)`",
        REGISTRY_LIFECYCLE,
        "registry lifecycle",
        d5_path,
        gfm,
    )
    serialization_index = require_exact_declaration(
        d5_text,
        r"19 行候选定义共同使用 `AssertionScopeSerializationId = ([^`]+)`",
        INTERNAL_SERIALIZATION_ID,
        "internal serialization",
        d5_path,
        gfm,
    )
    registry_rows, _table, table_index = require_top_level_gfm_table(
        gfm,
        ("ScopeId", "激活", "Version", "DefinitionHash", "归属 AssertionId"),
        "internal candidate scope table",
        d5_path,
        start_index,
        end_index,
    )
    if not (
        start_index < registry_heading_index
        < lifecycle_index < serialization_index
        < table_index < history_anchor_index < end_index
    ):
        raise VerificationError(
            f"{d5_path}: internal ScopeRegistry controls are out of canonical order"
        )
    blocks = parse_assertion_blocks(d5_text, gfm, d5_path)
    results: list[ScopeResult] = []
    referenced: list[str] = []
    actual_order: list[str] = []

    if len(registry_rows) != 19:
        raise VerificationError(
            f"internal registry must contain exactly 19 rows, got {len(registry_rows)}"
        )
    for cells in registry_rows:
        if not re.fullmatch(r"`[A-Z0-9-]+@[A-Z0-9]+`", cells[0]):
            raise VerificationError(f"invalid current ScopeId cell: {cells[0]!r}")
        scope_id = one_code_value(cells[0], "ScopeId")
        activation = cells[1]
        expected_activation = activation_from_scope(scope_id)
        if activation != expected_activation:
            raise VerificationError(
                f"{scope_id}: activation {activation!r} must match suffix {expected_activation!r}"
            )
        version = one_code_value(cells[2], "Version")
        stable_semver(version, f"{scope_id} Version")
        if version != "0.1.0":
            raise VerificationError(
                f"{scope_id}: draft candidate Version must remain '0.1.0' before first release"
            )
        stored = one_code_value(cells[3], "DefinitionHash")
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", stored):
            raise VerificationError(f"{scope_id}: invalid DefinitionHash {stored!r}")
        assertion_ids = tuple(re.findall(r"`(ASSERT-[A-Z0-9-]+)`", cells[4]))
        canonical_assertion_cell = "、".join(
            f"`{assertion_id}`" for assertion_id in assertion_ids
        )
        if not assertion_ids or cells[4] != canonical_assertion_cell:
            raise VerificationError(
                f"{scope_id}: AssertionId cell must contain only canonical backticked "
                "IDs joined by the Chinese enumeration comma"
            )
        if len(assertion_ids) != len(set(assertion_ids)):
            raise VerificationError(
                f"{scope_id}: duplicate AssertionId in current registry row"
            )
        try:
            assertion_blocks = tuple(blocks[item] for item in assertion_ids)
        except KeyError as exc:
            raise VerificationError(f"{scope_id}: missing {exc.args[0]} definition") from exc
        referenced.extend(assertion_ids)
        actual_order.append(scope_id)
        computed = sha256_hex(internal_payload(scope_id, version, assertion_blocks))
        results.append(
            ScopeResult(
                family="internal-candidate",
                serialization_id=INTERNAL_SERIALIZATION_ID,
                definition_source=INTERNAL_DEFINITION_SOURCE,
                activation=activation,
                scope_id=scope_id,
                version=version,
                assertion_ids=assertion_ids,
                stored_sha256=stored.removeprefix("sha256:"),
                computed_sha256=computed,
            )
        )

    if len(results) != 19 or len({item.scope_id for item in results}) != 19:
        raise VerificationError(
            f"internal registry must contain 19 unique scopes, got {len(results)}"
        )
    if actual_order != list(INTERNAL_CANDIDATE_ORDER):
        raise VerificationError(
            "internal candidate scopes are missing, duplicated, unknown, or reordered"
        )
    if len(referenced) != 28 or len(set(referenced)) != 28:
        raise VerificationError(
            f"internal registry must reference 28 unique assertions, got {len(referenced)}"
        )
    extra = set(blocks).difference(referenced)
    if extra:
        raise VerificationError(f"unregistered assertion definitions: {sorted(extra)}")
    mismatched = [item.scope_id for item in results if not item.definition_integrity]
    if mismatched:
        raise VerificationError(
            f"internal candidate DefinitionHash mismatch: {mismatched!r}"
        )
    return results


def verify_internal_history(d5_text: str, d5_path: Path) -> list[ScopeResult]:
    """Require an explicit, visible, empty formal-release history."""

    gfm = verified_gfm_document(d5_text, d5_path)
    history_anchor_index = require_top_level_anchor(
        gfm,
        '<a id="dd-scope-version-history"></a>',
        "internal release-history anchor",
        d5_path,
    )
    history_heading_index = require_top_level_heading(
        gfm,
        "###### 正式发布历史（当前为空）",
        6,
        "正式发布历史（当前为空）",
        "internal release-history heading",
        d5_path,
    )
    count_index = require_exact_declaration(
        d5_text,
        r"当前正式发布历史条数是 `([^`]+)`",
        "0",
        "formal release-history count",
        d5_path,
        gfm,
    )
    start_index = require_top_level_comment(
        gfm, HISTORY_START, "internal release-history start marker", d5_path
    )
    end_index = require_top_level_comment(
        gfm, HISTORY_END, "internal release-history end marker", d5_path
    )
    history_rows, _table, table_index = require_top_level_gfm_table(
        gfm,
        (
            "ScopeId",
            "已废止 Version",
            "已废止 DefinitionHash",
            "替代 Version",
            "变更原因",
            "回执处置",
        ),
        "internal formal release history table",
        d5_path,
        start_index,
        end_index,
        only_structure_between=True,
    )
    if not (
        history_anchor_index < history_heading_index < count_index < start_index
        < table_index < end_index
    ):
        raise VerificationError(
            f"{d5_path}: internal release-history controls are out of canonical order"
        )
    if history_rows:
        raise VerificationError(
            "formal release history must be empty: no scope has a protected tag, "
            "CI run, and approval record"
        )
    return []
def parse_named_fields(
    value: str, expected_names: tuple[str, ...], label: str
) -> dict[str, str]:
    parts = value.split(";")
    if len(parts) != len(expected_names):
        raise VerificationError(
            f"{label}: expected {len(expected_names)} ordered fields, got {len(parts)}"
        )
    parsed: dict[str, str] = {}
    for expected_name, part in zip(expected_names, parts):
        if part.count("=") != 1:
            raise VerificationError(f"{label}: malformed field {part!r}")
        name, field_value = part.split("=", 1)
        if name != expected_name:
            raise VerificationError(
                f"{label}: expected field {expected_name!r}, got {name!r}"
            )
        if not field_value or re.search(r"[;=`\s]", field_value):
            raise VerificationError(
                f"{label}: empty or non-canonical value for {name!r}"
            )
        parsed[name] = field_value
    return parsed


def verify_evaluated_profile_state(
    profile_key: str,
    domain: str,
    gate_id: str,
    source_id: str,
    evaluation_key: str,
    evaluation_receipt: str,
    outcome: str,
) -> None:
    """Reject terminal vendor states until a real release trust chain exists."""

    if outcome not in {"Passed", "Failed"}:
        raise VerificationError(f"invalid evaluated outcome: {outcome!r}")
    raise VerificationError(
        f"{profile_key}/{domain}/{gate_id}: terminal vendor state {outcome!r} is "
        f"not eligible while ScopeRegistry is {REGISTRY_LIFECYCLE}; source={source_id!r}, "
        f"evaluation_key={evaluation_key!r}, receipt={evaluation_receipt!r}"
    )

def verify_compat_profile_states(
    state_rows: list[list[str]], evidence_path: Path
) -> list[dict[str, str]]:
    actual: list[tuple[str, str, str, str, str, str, str, str]] = []
    seen: set[tuple[str, str]] = set()
    for cells in state_rows:
        profile_key = one_code_value(cells[0], "ProductProfileKey")
        domain_gate = re.fullmatch(
            r"(Trigger|SCPI|DCL-Status|Recall) / `(GATE-COMPAT-[A-Z0-9-]+)`",
            cells[1],
        )
        if not domain_gate:
            raise VerificationError(f"invalid compatibility Domain/Gate: {cells[1]!r}")
        domain, gate_id = domain_gate.groups()
        status = one_code_value(cells[2], "compatibility profile status")
        source_id = one_code_value(cells[3], "compatibility canonical source")
        evaluation_key = one_code_value(cells[4], "compatibility evaluation key")
        evaluation_receipt = one_code_value(cells[5], "compatibility evaluation receipt")
        gap_id = one_code_value(cells[6], "compatibility GAP")
        identity = (profile_key, domain)
        if identity in seen:
            raise VerificationError(f"duplicate compatibility profile state: {identity!r}")
        seen.add(identity)
        if status not in {"BlockedPendingEvidence", "Failed", "Passed"}:
            raise VerificationError(
                f"{profile_key}/{domain}: invalid compatibility status {status!r}"
            )
        if "PENDING" in source_id and status == "Passed":
            raise VerificationError(
                f"{profile_key}/{domain}: pending evidence cannot produce Passed"
            )
        if status == "BlockedPendingEvidence":
            if "PENDING" not in source_id:
                raise VerificationError(
                    f"{profile_key}/{domain}: pending state requires an explicit pending source"
                )
            if evaluation_key != "NotIssued" or evaluation_receipt != "NotIssued":
                raise VerificationError(
                    f"{profile_key}/{domain}: pending state cannot carry an issued key or receipt"
                )
        else:
            verify_evaluated_profile_state(
                profile_key,
                domain,
                gate_id,
                source_id,
                evaluation_key,
                evaluation_receipt,
                status,
            )
        actual.append(
            (
                profile_key,
                domain,
                gate_id,
                status,
                source_id,
                evaluation_key,
                evaluation_receipt,
                gap_id,
            )
        )
    if tuple(actual) != PENDING_COMPAT_PROFILE_STATES:
        raise VerificationError(
            "compatibility profile rows differ from the reviewed pending candidate matrix"
        )
    return [
        {
            "product_profile_key_pattern": profile_key,
            "domain": domain,
            "gate_id": gate_id,
            "status": "Pending",
            "source_id": source_id,
            "gap_id": gap_id,
        }
        for (
            profile_key,
            domain,
            gate_id,
            _status,
            source_id,
            _evaluation_key,
            _evaluation_receipt,
            gap_id,
        ) in actual
    ]


def verify_compat_history(
    history_rows: list[list[str]], evidence_path: Path
) -> list[ScopeResult]:
    if history_rows:
        raise VerificationError(
            f"{evidence_path}: compatibility formal-release history must be empty "
            "while no compatibility scope has protected Git/CI/approval records"
        )
    return []

def verify_compatibility(
    evidence_text: str, evidence_path: Path
) -> tuple[list[ScopeResult], list[ScopeResult], list[dict[str, str]]]:
    gfm = verified_gfm_document(evidence_text, evidence_path)
    start_index = require_top_level_anchor(
        gfm,
        COMPAT_START,
        "compatibility section anchor",
        evidence_path,
    )
    section_heading_index = require_top_level_heading(
        gfm,
        "### 兼容性验收门的唯一状态与断言",
        3,
        "兼容性验收门的唯一状态与断言",
        "compatibility section heading",
        evidence_path,
    )
    end_index = require_top_level_heading(
        gfm,
        COMPAT_END,
        2,
        "3. 可观察语义与架构约束",
        "compatibility section end heading",
        evidence_path,
    )
    serialization_index = require_exact_declaration(
        evidence_text,
        r"CompatibilityAssertionScopeSerializationId = ([^`]+)`",
        COMPAT_SERIALIZATION_ID,
        "compatibility serialization",
        evidence_path,
        gfm,
    )
    current_rows, _current_table, current_table_index = require_top_level_gfm_table(
        gfm,
        ("Domain / Gate", "ScopeId / Version", "DefinitionHash"),
        "compatibility candidate scope table",
        evidence_path,
        start_index,
        end_index,
    )
    history_heading_index = require_top_level_heading(
        gfm,
        "#### 兼容范围版本审计",
        4,
        "兼容范围版本审计",
        "compatibility scope-history heading",
        evidence_path,
    )
    history_start_index = require_top_level_comment(
        gfm,
        COMPAT_HISTORY_START,
        "compatibility scope-history start marker",
        evidence_path,
    )
    history_end_index = require_top_level_comment(
        gfm,
        COMPAT_HISTORY_END,
        "compatibility scope-history end marker",
        evidence_path,
    )
    history_rows, _history_table, history_table_index = require_top_level_gfm_table(
        gfm,
        (
            "ScopeId",
            "已废止 Version",
            "已废止 DefinitionHash",
            "替代 Version",
            "变更原因",
            "回执处置",
        ),
        "compatibility scope history table",
        evidence_path,
        history_start_index,
        history_end_index,
        only_structure_between=True,
    )
    assertion_rows, _assertion_table, assertion_table_index = (
        require_top_level_gfm_table(
            gfm,
            ("AssertionId", "唯一阻断断言", "关联决策"),
            "compatibility assertion table",
            evidence_path,
            start_index,
            end_index,
        )
    )
    state_rows, _state_table, state_table_index = require_top_level_gfm_table(
        gfm,
        (
            "ProductProfileKey 模式",
            "域 / Gate",
            "当前状态",
            "当前 canonical 输入",
            "完整 EvaluationKey",
            "评估回执",
            "独立 GAP",
        ),
        "compatibility profile state table",
        evidence_path,
        start_index,
        end_index,
    )
    if not (
        start_index < section_heading_index < serialization_index
        < current_table_index < history_heading_index < history_start_index
        < history_table_index < history_end_index < assertion_table_index
        < state_table_index < end_index
    ):
        raise VerificationError(
            f"{evidence_path}: compatibility controls are out of canonical order"
        )

    assertions: dict[str, tuple[str, tuple[str, ...]]] = {}
    for cells in assertion_rows:
        if not re.fullmatch(r"`ASSERT-COMPAT-[A-Z0-9-]+`", cells[0]):
            raise VerificationError(
                f"invalid compatibility AssertionId cell: {cells[0]!r}"
            )
        assertion_id = one_code_value(cells[0], "compatibility AssertionId")
        if assertion_id in assertions:
            raise VerificationError(
                f"duplicate compatibility AssertionId: {assertion_id}"
            )
        decisions = code_id_list(cells[2], "DEC", "compatibility DecisionId")
        assertions[assertion_id] = (cells[1], decisions)

    if len(current_rows) != 4:
        raise VerificationError(
            f"compatibility registry must contain exactly 4 rows, got {len(current_rows)}"
        )
    results: list[ScopeResult] = []
    actual_order: list[str] = []
    for cells in current_rows:
        domain_gate = re.fullmatch(
            r"(Trigger|SCPI|DCL-Status|Recall) / `(GATE-COMPAT-[A-Z0-9-]+)`",
            cells[0],
        )
        scope_version = re.fullmatch(
            r"`(COMPAT-[A-Z0-9-]+@PROFILE)` / `([^`]+)`", cells[1]
        )
        if not domain_gate or not scope_version:
            raise VerificationError(f"malformed compatibility current row: {cells!r}")
        domain_value, gate_value = domain_gate.groups()
        scope_id, version = scope_version.groups()
        stable_semver(version, f"{scope_id} Version")
        stored = one_code_value(cells[2], "compatibility DefinitionHash")
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", stored):
            raise VerificationError(f"{scope_id}: invalid compatibility DefinitionHash")
        expected = COMPATIBILITY_CANDIDATES.get(scope_id)
        if not expected:
            raise VerificationError(f"unexpected compatibility scope: {scope_id}")
        (
            domain,
            gate,
            assertion_id,
            expected_decisions,
        ) = expected
        if (domain_value, gate_value) != (domain, gate):
            raise VerificationError(f"{scope_id}: Domain/Gate mapping mismatch")
        try:
            assertion_text, decisions = assertions[assertion_id]
        except KeyError as exc:
            raise VerificationError(f"{scope_id}: missing {assertion_id}") from exc
        if decisions != expected_decisions:
            raise VerificationError(
                f"{scope_id}: Decision mapping must be {expected_decisions!r}, got {decisions!r}"
            )
        fields = (scope_id, version, assertion_id, assertion_text)
        if any("|" in item or "\n" in item or "\r" in item for item in fields):
            raise VerificationError(f"{scope_id}: compatibility field needs escaping")
        computed = sha256_hex("|".join(fields))
        actual_order.append(scope_id)
        results.append(
            ScopeResult(
                family="compatibility-candidate",
                serialization_id=COMPAT_SERIALIZATION_ID,
                definition_source=COMPAT_DEFINITION_SOURCE,
                activation=activation_from_scope(scope_id),
                scope_id=scope_id,
                version=version,
                assertion_ids=(assertion_id,),
                stored_sha256=stored.removeprefix("sha256:"),
                computed_sha256=computed,
            )
        )

    if actual_order != list(COMPAT_CANDIDATE_ORDER):
        raise VerificationError(
            "compatibility current rows are missing, duplicated, unknown, or reordered"
        )
    expected_assertion_ids = {
        item[2] for item in COMPATIBILITY_CANDIDATES.values()
    }
    if len(assertions) != 4 or set(assertions) != expected_assertion_ids:
        raise VerificationError(
            "compatibility assertion table must contain each of the four expected "
            "AssertionIds exactly once"
        )
    mismatched = [item.scope_id for item in results if not item.definition_integrity]
    if mismatched:
        raise VerificationError(
            f"compatibility candidate DefinitionHash mismatch: {mismatched!r}"
        )
    profile_states = verify_compat_profile_states(state_rows, evidence_path)
    history_results = verify_compat_history(history_rows, evidence_path)
    return results, history_results, profile_states


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise VerificationError(
            f"negative self-test fixture {label!r} must occur exactly once"
        )
    return text.replace(old, new, 1)


def expect_rejected(label: str, operation: Callable[[], object]) -> None:
    try:
        operation()
    except VerificationError:
        return
    raise VerificationError(f"negative self-test was not rejected: {label}")


def exact_gfm_table_block(
    text: str, source: Path, expected_header: tuple[str, ...], label: str
) -> str:
    """Extract one already-verified table token for mutation-only self-tests."""

    gfm = verified_gfm_document(text, source)
    matches: list[dict[str, object]] = []
    for table_index, table in enumerate(gfm_records(gfm, "tables", source)):
        header = table.get("header")
        if not isinstance(header, list):
            raise VerificationError(
                f"{source}: GFM table[{table_index}].header has an invalid schema"
            )
        semantic_header = tuple(
            normalize_gfm_text(
                cell,
                f"GFM table[{table_index}].header[{cell_index}]",
                source,
            )
            for cell_index, cell in enumerate(header)
        )
        if semantic_header == expected_header:
            matches.append(table)
    if len(matches) != 1 or not isinstance(matches[0].get("raw"), str):
        raise VerificationError(
            f"negative self-test fixture {label!r} must be one GFM table"
        )
    return str(matches[0]["raw"]).rstrip("\n")


def hidden_gfm_variants(block: str) -> tuple[tuple[str, str], ...]:
    """Place an exact protected block in five non-top-level GFM contexts."""

    blockquote = "\n".join(
        f"> {line}" if line else ">" for line in block.splitlines()
    )
    indented = "\n".join(f"    {line}" for line in block.splitlines())
    return (
        ("fenced", f"````markdown\n{block}\n````"),
        ("HTML comment", f"<!--\n{block}\n-->"),
        ("blockquote", blockquote),
        ("raw HTML", f"<div>\n{block}\n</div>"),
        ("indented", indented),
    )


def run_negative_self_tests(
    d5_text: str,
    d5_path: Path,
    evidence_text: str,
    evidence_path: Path,
    mapping_text: str,
    mapping_path: Path,
) -> int:
    tests: list[tuple[str, Callable[[], object]]] = []

    def add_hidden_structure_tests(
        label: str,
        source_text: str,
        block: str,
        verifier: Callable[[str], object],
    ) -> None:
        for context, hidden in hidden_gfm_variants(block):
            mutated = replace_once(
                source_text,
                block,
                hidden,
                f"{label} hidden in {context}",
            )
            tests.append(
                (
                    f"{label} hidden in {context}",
                    lambda mutated=mutated, verifier=verifier: verifier(mutated),
                )
            )

    verify_d5_policy_tables(d5_text, d5_path)
    current_scopes = verify_internal_current(d5_text, d5_path)
    protected_tables = (
        (
            "SCPI address binding table",
            d5_text,
            d5_path,
            ("厂商地址域", "精确产品目标", "额外一致性约束"),
            lambda mutated: verify_d5_policy_tables(mutated, d5_path),
        ),
        (
            "SCPI profile-address admission table",
            d5_text,
            d5_path,
            ("Profile 类别", "编号与选择规则的准入证据", "当前状态与允许行为"),
            lambda mutated: verify_d5_policy_tables(mutated, d5_path),
        ),
        (
            "target dependency allowlist",
            d5_text,
            d5_path,
            ("构建目标或目标组", "归属层", "允许直接依赖的内部层/目标"),
            lambda mutated: verify_d5_policy_tables(mutated, d5_path),
        ),
        (
            "lower-layer product-type denylist",
            d5_text,
            d5_path,
            ("作用范围", "public header / AST 中禁止的上层类型族", "应使用的边界"),
            lambda mutated: verify_d5_policy_tables(mutated, d5_path),
        ),
        (
            "Gate/Decision/GAP table",
            mapping_text,
            mapping_path,
            ("Gate / Scope", "AssertionId", "关键 Decision", "阻断性内部 GAP"),
            lambda mutated: verify_gate_decision_gap_matrix(
                mutated, mapping_path, current_scopes
            ),
        ),
        (
            "internal current table",
            d5_text,
            d5_path,
            ("ScopeId", "激活", "Version", "DefinitionHash", "归属 AssertionId"),
            lambda mutated: verify_internal_current(mutated, d5_path),
        ),
        (
            "internal history table",
            d5_text,
            d5_path,
            (
                "ScopeId",
                "已废止 Version",
                "已废止 DefinitionHash",
                "替代 Version",
                "变更原因",
                "回执处置",
            ),
            lambda mutated: verify_internal_history(mutated, d5_path),
        ),
        (
            "compatibility current table",
            evidence_text,
            evidence_path,
            ("Domain / Gate", "ScopeId / Version", "DefinitionHash"),
            lambda mutated: verify_compatibility(mutated, evidence_path),
        ),
        (
            "compatibility history table",
            evidence_text,
            evidence_path,
            (
                "ScopeId",
                "已废止 Version",
                "已废止 DefinitionHash",
                "替代 Version",
                "变更原因",
                "回执处置",
            ),
            lambda mutated: verify_compatibility(mutated, evidence_path),
        ),
        (
            "compatibility assertion table",
            evidence_text,
            evidence_path,
            ("AssertionId", "唯一阻断断言", "关联决策"),
            lambda mutated: verify_compatibility(mutated, evidence_path),
        ),
        (
            "compatibility profile-state table",
            evidence_text,
            evidence_path,
            (
                "ProductProfileKey 模式",
                "域 / Gate",
                "当前状态",
                "当前 canonical 输入",
                "完整 EvaluationKey",
                "评估回执",
                "独立 GAP",
            ),
            lambda mutated: verify_compatibility(mutated, evidence_path),
        ),
        (
            "evidence portability table",
            mapping_text,
            mapping_path,
            ("Evidence portability GAP", "治理 Decision", "只阻断什么"),
            lambda mutated: verify_evidence_portability_gap_map(
                mutated, mapping_path
            ),
        ),
    )
    for label, source_text, source_path, header, verifier in protected_tables:
        table_block = exact_gfm_table_block(
            source_text, source_path, header, label
        )
        add_hidden_structure_tests(label, source_text, table_block, verifier)

    znb_profile_row = "| " + " | ".join(EXPECTED_SCPI_PROFILE_ADMISSION[1]) + " |"
    enabled_znb_mapping = replace_once(
        d5_text,
        znb_profile_row,
        znb_profile_row.replace("BlockedPendingEvidence", "Enabled", 1),
        "premature ZNB address mapping",
    )
    tests.append(
        (
            "premature ZNB address mapping",
            lambda: verify_d5_policy_tables(enabled_znb_mapping, d5_path),
        )
    )

    l4_allowlist_row = "| " + " | ".join(EXPECTED_TARGET_DEPENDENCY_ALLOWLIST[5]) + " |"
    widened_l4_dependency = replace_once(
        d5_text,
        l4_allowlist_row,
        l4_allowlist_row.replace("| L0 |", "| L0、L7 |", 1),
        "widened L4 dependency allowlist",
    )
    tests.append(
        (
            "widened L4 dependency allowlist",
            lambda: verify_d5_policy_tables(widened_l4_dependency, d5_path),
        )
    )

    l0_denylist_row = "| " + " | ".join(EXPECTED_LOWER_LAYER_TYPE_DENYLIST[0]) + " |"
    weakened_l0_denylist = replace_once(
        d5_text,
        l0_denylist_row,
        l0_denylist_row.replace("、`ProfileAddressBook`", "", 1),
        "weakened L0 product-type denylist",
    )
    tests.append(
        (
            "weakened L0 product-type denylist",
            lambda: verify_d5_policy_tables(weakened_l0_denylist, d5_path),
        )
    )

    control_blocks = (
        (
            "SCPI address-selection anchor/heading",
            d5_text,
            f"{SCPI_ADDRESS_SELECTION_START}\n\n"
            "#### 10.1.2 厂商编号、产品对象与连接选择",
            lambda mutated: verify_d5_policy_tables(mutated, d5_path),
        ),
        (
            "target dependency-policy anchor/heading",
            d5_text,
            f"{TARGET_DEPENDENCY_POLICY_START}\n\n"
            "#### 12.1.1 构建目标直接依赖白名单",
            lambda mutated: verify_d5_policy_tables(mutated, d5_path),
        ),
        (
            "Gate control anchor/heading",
            mapping_text,
            f"{GATE_DECISION_GAP_START}\n\n"
            "## 验收门到决策与内部缺口的唯一映射",
            lambda mutated: verify_gate_decision_gap_matrix(
                mutated, mapping_path, current_scopes
            ),
        ),
        (
            "internal current control anchor/heading",
            d5_text,
            f"{INTERNAL_REGISTRY_START}\n\n"
            f"{INTERNAL_REGISTRY_NATIVE_HEADING}\n\n"
            "**候选定义｜DraftPendingReview**",
            lambda mutated: verify_internal_current(mutated, d5_path),
        ),
        (
            "internal history control marker",
            d5_text,
            HISTORY_START,
            lambda mutated: verify_internal_history(mutated, d5_path),
        ),
        (
            "compatibility control anchor/heading",
            evidence_text,
            f"{COMPAT_START}\n\n### 兼容性验收门的唯一状态与断言",
            lambda mutated: verify_compatibility(mutated, evidence_path),
        ),
        (
            "compatibility history control marker",
            evidence_text,
            COMPAT_HISTORY_START,
            lambda mutated: verify_compatibility(mutated, evidence_path),
        ),
        (
            "portability Decision anchor/heading",
            mapping_text,
            '<a id="dec-evidence-portability-001"></a>\n\n'
            "### `DEC-EVIDENCE-PORTABILITY-001`",
            lambda mutated: verify_evidence_portability_gap_map(
                mutated, mapping_path
            ),
        ),
    )
    for label, source_text, block, verifier in control_blocks:
        add_hidden_structure_tests(label, source_text, block, verifier)

    d5_gfm = verified_gfm_document(d5_text, d5_path)
    assertion_fixture = next(
        (
            str(item["raw"]).rstrip("\n")
            for item in gfm_records(
                d5_gfm, "topLevelAssertionItems", d5_path
            )
            if item.get("id") == "ASSERT-TYPE-L0-L3-001"
            and isinstance(item.get("raw"), str)
        ),
        None,
    )
    if assertion_fixture is None:
        raise VerificationError(
            "negative self-test fixture must include ASSERT-TYPE-L0-L3-001"
        )
    add_hidden_structure_tests(
        "internal assertion definition",
        d5_text,
        assertion_fixture,
        lambda mutated: verify_internal_current(mutated, d5_path),
    )

    for key_name, fields in (
        ("GateEvaluationKey", GATE_EVALUATION_KEY_FIELDS),
        (
            "CompatibilityGateEvaluationKey",
            COMPATIBILITY_GATE_EVALUATION_KEY_FIELDS,
        ),
    ):
        expected_lines = [f"  {field}," for field in fields[:-1]]
        expected_lines.append(f"  {fields[-1]}")
        key_block = "\n".join(
            (f"```text", f"{key_name} = {{", *expected_lines, "}", "```")
        )
        add_hidden_structure_tests(
            f"{key_name} block",
            d5_text,
            key_block,
            lambda mutated, key_name=key_name, fields=fields: (
                verify_exact_key_block(mutated, key_name, fields, d5_path)
            ),
        )

    trigger_row = (
        "| Trigger / `GATE-COMPAT-TRIGGER-001` | "
        "`COMPAT-TRIGGER@PROFILE` / `1.0.0` | "
        "`sha256:41bfabdddeb074b4ba282a77ed63e0f37835dfe3e9be3d5a6a9ccfafe755b0d9` |"
    )
    missing_current = replace_once(
        evidence_text, f"{trigger_row}\n", "", "missing compatibility current row"
    )
    tests.append(
        (
            "missing compatibility current row",
            lambda: verify_compatibility(missing_current, evidence_path),
        )
    )
    unknown_current = replace_once(
        evidence_text,
        trigger_row,
        trigger_row.replace("COMPAT-TRIGGER@PROFILE", "COMPAT-UNKNOWN@PROFILE"),
        "unknown compatibility current row",
    )
    tests.append(
        (
            "unknown compatibility current row",
            lambda: verify_compatibility(unknown_current, evidence_path),
        )
    )

    old_assertion = (
        "按声明能力回放 source/scope/mode/granularity；忙时的接纳、排队、合并或拒绝结果"
        "与 transcript 逐项一致"
    )
    changed_assertion = f"{old_assertion}，并原地扩大语义"
    changed_hash = sha256_hex(
        "|".join(
            (
                "COMPAT-TRIGGER@PROFILE",
                "1.0.0",
                "ASSERT-COMPAT-TRIGGER-001",
                changed_assertion,
            )
        )
    )
    same_version_without_rehash = replace_once(
        evidence_text,
        old_assertion,
        changed_assertion,
        "same-version compatibility assertion mutation",
    )
    tests.append(
        (
            "same-version compatibility mutation without recomputed hash",
            lambda: verify_compatibility(same_version_without_rehash, evidence_path),
        )
    )
    same_version_rehash = replace_once(
        same_version_without_rehash,
        "41bfabdddeb074b4ba282a77ed63e0f37835dfe3e9be3d5a6a9ccfafe755b0d9",
        changed_hash,
        "same-version compatibility rehash",
    )
    # Candidate definitions may change semantics and rehash in place before
    # first formal release. This is an explicit positive guard, not a Gate pass.
    verify_compatibility(same_version_rehash, evidence_path)

    decision_row = (
        "| `ASSERT-COMPAT-TRIGGER-001` | "
        f"{old_assertion} | `DEC-TRG-MODEL-001`、`DEC-TRG-CARD-001` |"
    )
    missing_decision = replace_once(
        evidence_text,
        decision_row,
        decision_row.replace("、`DEC-TRG-CARD-001`", ""),
        "missing compatibility Decision",
    )
    tests.append(
        (
            "missing compatibility Decision mapping",
            lambda: verify_compatibility(missing_decision, evidence_path),
        )
    )

    pending_passed = replace_once(
        evidence_text,
        "| `ZNB/{model}/{firmware}/{options}` | Trigger / `GATE-COMPAT-TRIGGER-001` | `BlockedPendingEvidence` |",
        "| `ZNB/{model}/{firmware}/{options}` | Trigger / `GATE-COMPAT-TRIGGER-001` | `Passed` |",
        "pending source marked Passed",
    )
    tests.append(
        (
            "pending source marked Passed",
            lambda: verify_compatibility(pending_passed, evidence_path),
        )
    )

    pending_state_row = (
        "| `ZNB/{model}/{firmware}/{options}` | Trigger / `GATE-COMPAT-TRIGGER-001` | "
        "`BlockedPendingEvidence` | `SRC-ZNB-TRANSCRIPT-PENDING` | `NotIssued` | "
        "`NotIssued` | `GAP-ZNB-TRIGGER-TRANSCRIPT-001` |"
    )
    passed_without_key = replace_once(
        evidence_text,
        pending_state_row,
        "| `ZNB/ZNB20/4.70/SP100` | Trigger / `GATE-COMPAT-TRIGGER-001` | "
        "`Passed` | `SRC-ZNB-TRANSCRIPT-ZNB20-470-SP100` | `NotIssued` | "
        "`NotIssued` | `GAP-ZNB-TRIGGER-TRANSCRIPT-001` |",
        "Passed profile missing exact key and receipt",
    )
    tests.append(
        (
            "Passed profile missing exact key and receipt",
            lambda: verify_compatibility(passed_without_key, evidence_path),
        )
    )

    failed_without_key = passed_without_key.replace("`Passed`", "`Failed`", 1)
    tests.append(
        (
            "Failed profile missing exact key and receipt",
            lambda: verify_compatibility(failed_without_key, evidence_path),
        )
    )

    fake_digest = "0" * 64
    exact_revision = lambda name: f"{name}@sha256:{fake_digest}"
    fake_key_values = {
        "ProductProfileKey": "ZNB/ZNB20/4.70/SP100",
        "CompatibilityDomain": "Trigger",
        "CompatibilityGateId": "GATE-COMPAT-TRIGGER-001",
        "TransportProfileRevision": exact_revision("transport"),
        "ProtocolProfileRevision": exact_revision("protocol"),
        "RuntimeProfileRevision": exact_revision("runtime"),
        "StorageProfileRevision": "None@compat-storage-v1",
        "CompatibilityAssertionScopeId": "COMPAT-TRIGGER@PROFILE",
        "CompatibilityAssertionScopeVersion": "1.0.0",
        "CompatibilityAssertionScopeDefinitionHash": (
            "sha256:41bfabdddeb074b4ba282a77ed63e0f37835dfe3e9be3d5a6a9ccfafe755b0d9"
        ),
        "CompatibilityAssertionScopeSerializationId": COMPAT_SERIALIZATION_ID,
        "CodeRevision": exact_revision("code"),
        "BuildRevision": exact_revision("build"),
        "ExactCapabilityRevision": exact_revision("capability"),
        "TranscriptAndTestBundleHash": f"sha256:{fake_digest}",
    }
    fake_key = ";".join(
        f"{field}={fake_key_values[field]}"
        for field in COMPATIBILITY_GATE_EVALUATION_KEY_FIELDS
    )
    fake_receipt_values = {
        "ReceiptSerializationId": COMPAT_RECEIPT_SERIALIZATION_ID,
        "Outcome": "Passed",
        "EvaluationKeyHash": f"sha256:{sha256_hex(fake_key)}",
        "EvidenceBundleHash": f"sha256:{fake_digest}",
        "EvaluationDefinitionHash": f"sha256:{fake_digest}",
        "ProtectedGitRef": "refs/tags/compat-test",
        "CiRunRef": "ci-run-123",
        "ApprovalRecordRef": "approval-123",
    }
    fake_receipt = ";".join(
        f"{field}={fake_receipt_values[field]}"
        for field in COMPATIBILITY_EVALUATION_RECEIPT_FIELDS
    )
    tests.append(
        (
            "structurally complete but unverified Passed receipt",
            lambda: verify_evaluated_profile_state(
                "ZNB/ZNB20/4.70/SP100",
                "Trigger",
                "GATE-COMPAT-TRIGGER-001",
                "SRC-ZNB-TRANSCRIPT-ZNB20-470-SP100",
                fake_key,
                fake_receipt,
                "Passed",
            ),
        )
    )

    leading_zero_semver = replace_once(
        evidence_text,
        "`COMPAT-TRIGGER@PROFILE` / `1.0.0`",
        "`COMPAT-TRIGGER@PROFILE` / `01.0.0`",
        "leading-zero compatibility SemVer",
    )
    tests.append(
        (
            "leading-zero compatibility SemVer",
            lambda: verify_compatibility(leading_zero_semver, evidence_path),
        )
    )

    sna_recall_row = (
        "| `SNA/{model}/{firmware}/{options}` | Recall / `GATE-COMPAT-RECALL-001` | "
        "`BlockedPendingEvidence` | `SRC-SNA-TRANSCRIPT-PENDING` | `NotIssued` | "
        "`NotIssued` | `GAP-SNA-RECALL-TRANSCRIPT-001` |\n"
    )
    missing_profile_state = replace_once(
        evidence_text, sna_recall_row, "", "missing compatibility profile state"
    )
    tests.append(
        (
            "missing compatibility profile state",
            lambda: verify_compatibility(missing_profile_state, evidence_path),
        )
    )

    rollback_row = (
        "| `COMPAT-TRIGGER@PROFILE` | `1.0.0` | "
        "`sha256:41bfabdddeb074b4ba282a77ed63e0f37835dfe3e9be3d5a6a9ccfafe755b0d9` | "
        "`0.9.0` | 试图把 current 回退到更低版本 | "
        "旧回执只读审计，不得给 `0.9.0` 提供 Passed |\n\n"
    )
    empty_history_tail = (
        "| --- | --- | --- | --- | --- | --- |\n\n"
        f"{COMPAT_HISTORY_END}"
    )
    rollback_history = replace_once(
        evidence_text,
        empty_history_tail,
        "| --- | --- | --- | --- | --- | --- |\n"
        f"{rollback_row}{COMPAT_HISTORY_END}",
        "compatibility history rollback",
    )
    tests.append(
        (
            "compatibility history rollback",
            lambda: verify_compatibility(rollback_history, evidence_path),
        )
    )

    compat_history_start_at = evidence_text.index(COMPAT_HISTORY_START)
    compat_history_end_at = evidence_text.index(
        COMPAT_HISTORY_END, compat_history_start_at
    ) + len(COMPAT_HISTORY_END)
    compat_history_block = evidence_text[
        compat_history_start_at:compat_history_end_at
    ]
    duplicated_compat_history_outside = (
        f"{evidence_text}\n\n{compat_history_block}\n"
    )
    tests.append(
        (
            "compatibility history duplicated after outer section",
            lambda: verify_compatibility(
                duplicated_compat_history_outside, evidence_path
            ),
        )
    )

    unknown_internal = replace_once(
        d5_text,
        "| `TYPE-L0-L3@R1` | R1 |",
        "| `UNKNOWN@R1` | R1 |",
        "unknown internal current row",
    )
    tests.append(
        (
            "unknown internal current row",
            lambda: verify_internal_current(unknown_internal, d5_path),
        )
    )
    first_internal = current_scopes[0]
    current_assertion_row = (
        f"| `{first_internal.scope_id}` | {first_internal.activation} | "
        f"`{first_internal.version}` | `sha256:{first_internal.stored_sha256}` | "
        "`ASSERT-TYPE-L0-L3-001` |"
    )
    assertion_with_trailing_text = replace_once(
        d5_text,
        current_assertion_row,
        current_assertion_row.replace(
            "`ASSERT-TYPE-L0-L3-001` |",
            "`ASSERT-TYPE-L0-L3-001` 候选界外尾随文本 |",
        ),
        "internal current AssertionId trailing text",
    )
    tests.append(
        (
            "internal current AssertionId trailing text",
            lambda: verify_internal_current(assertion_with_trailing_text, d5_path),
        )
    )
    wrong_lifecycle = replace_once(
        d5_text,
        "当前注册表生命周期是 `DraftPendingReview`",
        "当前注册表生命周期是 `Released`",
        "draft registry marked Released",
    )
    tests.append(
        (
            "draft registry marked Released",
            lambda: verify_internal_current(wrong_lifecycle, d5_path),
        )
    )
    empty_internal_history_tail = (
        "| --- | --- | --- | --- | --- | --- |\n"
        f"{HISTORY_END}"
    )
    fake_internal_history = replace_once(
        d5_text,
        empty_internal_history_tail,
        "| --- | --- | --- | --- | --- | --- |\n"
        "| `TYPE-L0-L3@R1` | `0.0.1` | "
        "`sha256:0000000000000000000000000000000000000000000000000000000000000000` | "
        "`0.1.0` | 从未签发的伪历史 | 无有效回执 |\n"
        f"{HISTORY_END}",
        "fabricated internal release history",
    )
    tests.append(
        (
            "fabricated internal release history",
            lambda: verify_internal_history(fake_internal_history, d5_path),
        )
    )

    history_start_at = d5_text.index(HISTORY_START)
    history_end_at = d5_text.index(HISTORY_END, history_start_at) + len(HISTORY_END)
    history_block = d5_text[history_start_at:history_end_at]
    duplicated_history_section = replace_once(
        d5_text,
        history_block,
        f"{history_block}\n\n{history_block}",
        "duplicated internal history marker and table",
    )
    tests.append(
        (
            "duplicated internal history marker and table",
            lambda: verify_internal_history(duplicated_history_section, d5_path),
        )
    )

    portability_mapping_row = (
        "| `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` | "
        "`DEC-EVIDENCE-PORTABILITY-001` | "
        "离线独立复核、对外发布证据包，以及明确要求可移植证据的放行 |"
    )
    portability_source_row = (
        "| `GAP-SNA-SOURCE-PROVENANCE-001` | "
        "`DEC-EVIDENCE-PORTABILITY-001` | "
        "宣称对象来自官方原包、跨厂商来源归属，以及要求官方来源真实性的放行 |"
    )
    portability_anchor = '<a id="dec-evidence-portability-001"></a>'
    portability_heading = "### `DEC-EVIDENCE-PORTABILITY-001`"

    def with_hidden_portability_examples(source: str) -> str:
        return (
            f"{source.rstrip()}\n\n```text\n"
            "| Evidence portability GAP | 治理 Decision | 只阻断什么 |\n"
            "| --- | --- | --- |\n"
            f"{portability_mapping_row}\n\n"
            f"{portability_anchor}\n\n"
            f"{portability_heading}\n"
            "```\n\n"
            "<!--\n"
            "| Evidence portability GAP | 治理 Decision | 只阻断什么 |\n"
            "| --- | --- | --- |\n"
            f"{portability_mapping_row}\n\n"
            f"{portability_anchor}\n\n"
            f"{portability_heading}\n"
            "-->\n\n"
            "    ### `DEC-EVIDENCE-PORTABILITY-001`\n"
            '    <a id="dec-evidence-portability-001"></a>\n\n'
            "> ```text\n"
            "> ### `DEC-EVIDENCE-PORTABILITY-001`\n"
            '> <a id="dec-evidence-portability-001"></a>\n'
            "> ```\n"
        )

    # This positive check runs before replacing the real document with the
    # isolated mutation fixture. A caller may already have hidden exact
    # heading/anchor examples; self-test setup must not count their raw strings.
    verify_evidence_portability_gap_map(
        with_hidden_portability_examples(mapping_text), mapping_path
    )

    mapping_text = (
        "# Evidence portability verifier fixture\n\n"
        f"{EVIDENCE_PORTABILITY_GAP_MAP_START}\n"
        "| Evidence portability GAP | 治理 Decision | 只阻断什么 |\n"
        "| --- | --- | --- |\n"
        f"{portability_mapping_row}\n"
        f"{portability_source_row}\n"
        f"{EVIDENCE_PORTABILITY_GAP_MAP_END}\n\n"
        f"{portability_anchor}\n\n"
        f"{portability_heading}\n"
    )
    verify_evidence_portability_gap_map(mapping_text, mapping_path)

    wrong_portability_decision = replace_once(
        mapping_text,
        portability_mapping_row,
        portability_mapping_row.replace(
            "DEC-EVIDENCE-PORTABILITY-001", "DEC-LAYER-PURECORE-001"
        ),
        "remapped evidence portability GAP",
    )
    tests.append(
        (
            "remapped evidence portability GAP",
            lambda: verify_evidence_portability_gap_map(
                wrong_portability_decision, mapping_path
            ),
        )
    )

    renamed_portability_heading = replace_once(
        mapping_text,
        portability_heading,
        "### `DEC-EVIDENCE-PORTABILITY-RENAMED`",
        "renamed evidence portability Decision heading",
    )
    tests.append(
        (
            "renamed evidence portability Decision heading",
            lambda: verify_evidence_portability_gap_map(
                renamed_portability_heading, mapping_path
            ),
        )
    )

    duplicated_portability_heading = replace_once(
        mapping_text,
        portability_heading,
        f"{portability_heading}\n\n{portability_heading}",
        "duplicated evidence portability Decision heading",
    )
    tests.append(
        (
            "duplicated evidence portability Decision heading",
            lambda: verify_evidence_portability_gap_map(
                duplicated_portability_heading, mapping_path
            ),
        )
    )

    portability_definition_head = (
        f"{portability_anchor}\n\n{portability_heading}"
    )
    moved_portability_anchor = replace_once(
        mapping_text,
        portability_definition_head,
        portability_heading,
        "moved evidence portability Decision anchor",
    )
    moved_portability_anchor = (
        f"{moved_portability_anchor.rstrip()}\n\n{portability_anchor}\n"
    )
    tests.append(
        (
            "moved evidence portability Decision anchor",
            lambda: verify_evidence_portability_gap_map(
                moved_portability_anchor, mapping_path
            ),
        )
    )

    conflicting_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "| Evidence portability GAP | 治理 Decision | 只阻断什么 |\n"
        "| --- | --- | --- |\n"
        "| `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` | "
        "`DEC-LAYER-PURECORE-001` | 未签发的界外冲突映射 |\n"
    )
    tests.append(
        (
            "conflicting evidence portability table outside markers",
            lambda: verify_evidence_portability_gap_map(
                conflicting_portability_table, mapping_path
            ),
        )
    )

    no_outer_pipe_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "Evidence portability GAP | 治理 Decision | 只阻断什么\n"
        "--- | --- | ---\n"
        "`GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` | "
        "`DEC-LAYER-PURECORE-001` | 无外侧竖线的冲突映射\n"
    )
    tests.append(
        (
            "outer-pipe-free conflicting portability table",
            lambda: verify_evidence_portability_gap_map(
                no_outer_pipe_portability_table, mapping_path
            ),
        )
    )

    whitespace_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "  |  Evidence   portability GAP  | 治理   Decision | 只阻断什么  |\n"
        "  | --- | --- | --- |\n"
        "  |  `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001`  | "
        "`DEC-LAYER-PURECORE-001`  | 空白变体冲突映射 |\n"
    )
    tests.append(
        (
            "whitespace-equivalent conflicting portability table",
            lambda: verify_evidence_portability_gap_map(
                whitespace_portability_table, mapping_path
            ),
        )
    )

    escaped_pipe_link_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "| [Evidence portability GAP](#header\\|target) | 治理 Decision | 只阻断什么 |\n"
        "| --- | --- | --- |\n"
        "| [`GAP-SNA-OBJDUMP-REPORT-BUNDLE-001`](#row\\|target) | "
        "`DEC-LAYER-PURECORE-001` | escaped-pipe link 冲突映射 |\n"
    )
    tests.append(
        (
            "escaped-pipe link-target portability table",
            lambda: verify_evidence_portability_gap_map(
                escaped_pipe_link_portability_table, mapping_path
            ),
        )
    )

    double_space_portability_heading = replace_once(
        mapping_text,
        portability_heading,
        "###  `DEC-EVIDENCE-PORTABILITY-001`",
        "double-space evidence portability Decision heading",
    )
    tests.append(
        (
            "double-space evidence portability Decision heading",
            lambda: verify_evidence_portability_gap_map(
                double_space_portability_heading, mapping_path
            ),
        )
    )

    single_quote_portability_anchor = replace_once(
        mapping_text,
        portability_anchor,
        "<a id='dec-evidence-portability-001'></a>",
        "single-quote evidence portability Decision anchor",
    )
    tests.append(
        (
            "single-quote evidence portability Decision anchor",
            lambda: verify_evidence_portability_gap_map(
                single_quote_portability_anchor, mapping_path
            ),
        )
    )

    commented_portability_definition = replace_once(
        mapping_text,
        portability_definition_head,
        f"<!--\n{portability_definition_head}\n-->",
        "commented evidence portability Decision definition",
    )
    tests.append(
        (
            "commented evidence portability Decision definition",
            lambda: verify_evidence_portability_gap_map(
                commented_portability_definition, mapping_path
            ),
        )
    )

    fenced_portability_definition = replace_once(
        mapping_text,
        portability_definition_head,
        f"```text\n{portability_definition_head}\n```",
        "fenced evidence portability Decision definition",
    )
    tests.append(
        (
            "fenced evidence portability Decision definition",
            lambda: verify_evidence_portability_gap_map(
                fenced_portability_definition, mapping_path
            ),
        )
    )

    raw_block_portability_definition = replace_once(
        mapping_text,
        portability_definition_head,
        f"<div>\n{portability_definition_head}\n</div>",
        "raw-block evidence portability Decision definition",
    )
    tests.append(
        (
            "raw-block evidence portability Decision definition",
            lambda: verify_evidence_portability_gap_map(
                raw_block_portability_definition, mapping_path
            ),
        )
    )

    portability_section_start = mapping_text.index(
        EVIDENCE_PORTABILITY_GAP_MAP_START
    )
    portability_section_end = mapping_text.index(
        EVIDENCE_PORTABILITY_GAP_MAP_END, portability_section_start
    ) + len(EVIDENCE_PORTABILITY_GAP_MAP_END)
    portability_control_block = mapping_text[
        portability_section_start:portability_section_end
    ]
    fenced_portability_control_block = replace_once(
        mapping_text,
        portability_control_block,
        f"```text\n{portability_control_block}\n```",
        "fenced evidence portability control block",
    )
    tests.append(
        (
            "fenced evidence portability control block",
            lambda: verify_evidence_portability_gap_map(
                fenced_portability_control_block, mapping_path
            ),
        )
    )

    raw_html_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "<table><thead><tr><th>Evidence portability GAP</th>"
        "<th>治理 Decision</th><th>只阻断什么</th></tr></thead>"
        "<tbody><tr><td><code>GAP-SNA-OBJDUMP-REPORT-BUNDLE-001</code></td>"
        "<td><code>DEC-LAYER-PURECORE-001</code></td>"
        "<td>raw HTML 冲突映射</td></tr></tbody></table>\n"
    )
    tests.append(
        (
            "raw HTML conflicting portability table",
            lambda: verify_evidence_portability_gap_map(
                raw_html_portability_table, mapping_path
            ),
        )
    )

    implicit_close_raw_html_table = (
        f"{mapping_text.rstrip()}\n\n"
        "<table><tr><th>Evidence portability GAP<th>治理 Decision<th>只阻断什么\n"
        "<tr><td>GAP-SNA-OBJDUMP-REPORT-BUNDLE-001"
        "<td>DEC-LAYER-PURECORE-001<td>implicit-close 冲突映射\n"
    )
    tests.append(
        (
            "implicit-close raw HTML portability table",
            lambda: verify_evidence_portability_gap_map(
                implicit_close_raw_html_table, mapping_path
            ),
        )
    )

    implicit_close_raw_html_heading = (
        f"{mapping_text.rstrip()}\n\n"
        "<h3>DEC-EVIDENCE-PORTABILITY-001\n"
    )
    tests.append(
        (
            "implicit-close raw HTML Decision heading",
            lambda: verify_evidence_portability_gap_map(
                implicit_close_raw_html_heading, mapping_path
            ),
        )
    )

    comment_sandwich_variants = (
        (
            "comment-sandwich active anchor",
            '<!--before--><a id="dec-evidence-portability-001"></a><!--after-->',
        ),
        (
            "comment-sandwich active table",
            "<!--before--><table><tr><td>GAP-SNA-OBJDUMP-REPORT-BUNDLE-001"
            "</td></tr></table><!--after-->",
        ),
        (
            "comment-sandwich active heading",
            "<!--before--><h3>DEC-EVIDENCE-PORTABILITY-001</h3><!--after-->",
        ),
        (
            "malformed-comment greater-than recovery",
            '<!--><a id="dec-evidence-portability-001"></a><!--tail-->',
        ),
        (
            "malformed-comment arrow recovery",
            '<!---><a id="dec-evidence-portability-001"></a><!--tail-->',
        ),
        (
            "malformed-comment bang-close recovery",
            '<!--x--!><a id="dec-evidence-portability-001"></a><!--tail-->',
        ),
    )
    for variant_label, variant in comment_sandwich_variants:
        mutated = f"{mapping_text.rstrip()}\n\n{variant}\n"
        tests.append(
            (
                variant_label,
                lambda mutated=mutated: verify_evidence_portability_gap_map(
                    mutated, mapping_path
                ),
            )
        )

    commented_inline_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "| Evidence portability G<!--split-->AP | 治理 Decision | 只阻断什么 |\n"
        "| --- | --- | --- |\n"
        "| `GAP-SNA-<!--split-->OBJDUMP-REPORT-BUNDLE-001` | "
        "`DEC-LAYER-PURECORE-001` | inline comment 冲突映射 |\n"
    )
    tests.append(
        (
            "inline-comment-equivalent portability table",
            lambda: verify_evidence_portability_gap_map(
                commented_inline_portability_table, mapping_path
            ),
        )
    )

    emphasized_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "| Evidence portability **GAP** | 治理 Decision | 只阻断什么 |\n"
        "| --- | --- | --- |\n"
        "| `GAP-SNA-**OBJDUMP**-REPORT-BUNDLE-001` | "
        "`DEC-LAYER-PURECORE-001` | emphasis 冲突映射 |\n"
    )
    tests.append(
        (
            "inline-emphasis-equivalent portability table",
            lambda: verify_evidence_portability_gap_map(
                emphasized_portability_table, mapping_path
            ),
        )
    )

    duplicate_heading_variants = (
        (
            "Setext evidence portability Decision heading",
            "`DEC-EVIDENCE-PORTABILITY-001`\n---",
        ),
        (
            "comment-suffixed evidence portability Decision heading",
            "### `DEC-EVIDENCE-PORTABILITY-001` <!-- duplicate -->",
        ),
        (
            "entity-encoded evidence portability Decision heading",
            "### `DEC&#45;EVIDENCE&#45;PORTABILITY&#45;001`",
        ),
        (
            "linked evidence portability Decision heading",
            "### [`DEC-EVIDENCE-PORTABILITY-001`](#duplicate)",
        ),
        (
            "shortcut-reference evidence portability Decision heading",
            "### [DEC-EVIDENCE-PORTABILITY-001]\n\n"
            "[DEC-EVIDENCE-PORTABILITY-001]: #duplicate",
        ),
        (
            "emphasized evidence portability Decision heading",
            "### **`DEC-EVIDENCE-PORTABILITY-001`**",
        ),
        (
            "raw HTML evidence portability Decision heading",
            '<h3 id="duplicate-heading">DEC-EVIDENCE-PORTABILITY-001</h3>',
        ),
    )
    for variant_label, variant in duplicate_heading_variants:
        mutated = f"{mapping_text.rstrip()}\n\n{variant}\n"
        tests.append(
            (
                variant_label,
                lambda mutated=mutated: verify_evidence_portability_gap_map(
                    mutated, mapping_path
                ),
            )
        )

    container_heading_variants = (
        (
            "blockquote evidence portability Decision heading",
            "> ### `DEC-EVIDENCE-PORTABILITY-001`",
        ),
        (
            "list evidence portability Decision heading",
            "- ### `DEC-EVIDENCE-PORTABILITY-001`",
        ),
        (
            "list-continuation evidence portability Decision heading",
            "- nested item\n\n    ### `DEC-EVIDENCE-PORTABILITY-001`",
        ),
    )
    for variant_label, variant in container_heading_variants:
        mutated = f"{mapping_text.rstrip()}\n\n{variant}\n"
        tests.append(
            (
                variant_label,
                lambda mutated=mutated: verify_evidence_portability_gap_map(
                    mutated, mapping_path
                ),
            )
        )

    blockquote_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "> | Evidence portability GAP | 治理 Decision | 只阻断什么 |\n"
        "> | --- | --- | --- |\n"
        "> | `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` | "
        "`DEC-LAYER-PURECORE-001` | blockquote 冲突映射 |\n"
    )
    tests.append(
        (
            "blockquote conflicting portability table",
            lambda: verify_evidence_portability_gap_map(
                blockquote_portability_table, mapping_path
            ),
        )
    )

    list_continuation_portability_table = (
        f"{mapping_text.rstrip()}\n\n"
        "- nested item\n\n"
        "    | Evidence portability GAP | 治理 Decision | 只阻断什么 |\n"
        "    | --- | --- | --- |\n"
        "    | `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` | "
        "`DEC-LAYER-PURECORE-001` | list continuation 冲突映射 |\n"
    )
    tests.append(
        (
            "list-continuation conflicting portability table",
            lambda: verify_evidence_portability_gap_map(
                list_continuation_portability_table, mapping_path
            ),
        )
    )

    duplicate_fragment_variants = (
        (
            "span evidence portability fragment",
            '<span id="dec-evidence-portability-001"></span>',
        ),
        (
            "legacy name evidence portability fragment",
            '<a name="dec-evidence-portability-001"></a>',
        ),
        (
            "entity-encoded evidence portability fragment",
            '<a id="dec-evidence&#45;portability-001"></a>',
        ),
        (
            "multiline evidence portability fragment",
            '<a\n id="dec-evidence-portability-001"></a>',
        ),
    )
    for variant_label, variant in duplicate_fragment_variants:
        mutated = f"{mapping_text.rstrip()}\n\n{variant}\n"
        tests.append(
            (
                variant_label,
                lambda mutated=mutated: verify_evidence_portability_gap_map(
                    mutated, mapping_path
                ),
            )
        )

    malformed_anchor_variants = (
        ("unclosed canonical-shape anchor", '<a id="unclosed-anchor">'),
        ("orphan anchor close", "</a>"),
        (
            "active-text anchor wrapper",
            '<a id="active-wrapper">active text</a>',
        ),
    )
    for variant_label, variant in malformed_anchor_variants:
        mutated = f"{mapping_text.rstrip()}\n\n{variant}\n"
        tests.append(
            (
                variant_label,
                lambda mutated=mutated: verify_evidence_portability_gap_map(
                    mutated, mapping_path
                ),
            )
        )

    # Non-rendered examples are intentionally ignored. This second positive
    # guard proves the isolated fixture has the same behavior as the real file.
    verify_evidence_portability_gap_map(
        with_hidden_portability_examples(mapping_text), mapping_path
    )

    if len(tests) != EXPECTED_NEGATIVE_GUARD_COUNT:
        raise VerificationError(
            "negative self-test inventory changed without updating its reviewed count: "
            f"expected {EXPECTED_NEGATIVE_GUARD_COUNT}, got {len(tests)}"
        )
    for label, operation in tests:
        expect_rejected(label, operation)
    return len(tests)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="VnaPro repository root; defaults to the verifier's repository",
    )
    parser.add_argument(
        "--manifest",
        action="store_true",
        help="emit the computed manifest as deterministic JSON",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="also prove that representative malformed registries are rejected",
    )
    args = parser.parse_args()

    root = args.repo_root.resolve()
    # D5 is navigation-only after the detailed-design split. SCPI policy lives
    # in D5A; dependency policy, Gate keys, assertions and ScopeRegistry live in
    # D5C. Verify one deterministic read-only view of both authoritative files
    # so the existing cross-policy negative tests still exercise the full set.
    d5a_path = root / "docs/详细设计/05A-外部适配器.md"
    d5_path = root / "docs/详细设计/05C-依赖验收与竞态.md"
    evidence_path = root / "docs/架构依据/01-商用品行为证据.md"
    mapping_path = root / "docs/架构依据/04-决策与验收映射.md"

    try:
        d5_text = (
            read_utf8_lf(d5a_path).rstrip("\n")
            + "\n\n"
            + read_utf8_lf(d5_path)
        )
        evidence_text = read_utf8_lf(evidence_path)
        mapping_text = read_utf8_lf(mapping_path)
        verify_exact_key_block(
            d5_text,
            "GateEvaluationKey",
            GATE_EVALUATION_KEY_FIELDS,
            d5_path,
        )
        verify_exact_key_block(
            d5_text,
            "CompatibilityGateEvaluationKey",
            COMPATIBILITY_GATE_EVALUATION_KEY_FIELDS,
            d5_path,
        )
        verify_d5_policy_tables(d5_text, d5_path)
        internal_candidates = verify_internal_current(d5_text, d5_path)
        internal_history = verify_internal_history(d5_text, d5_path)
        verify_gate_decision_gap_matrix(
            mapping_text, mapping_path, internal_candidates
        )
        verify_evidence_portability_gap_map(mapping_text, mapping_path)
        (
            compatibility_candidates,
            compatibility_history,
            profile_states,
        ) = verify_compatibility(evidence_text, evidence_path)
        groups = {
            "internal-candidates": internal_candidates,
            "internal-release-history": internal_history,
            "compatibility-candidates": compatibility_candidates,
            "compatibility-release-history": compatibility_history,
        }
        failed = [
            item
            for values in groups.values()
            for item in values
            if not item.definition_integrity
        ]
        if failed:
            details = ", ".join(
                f"{item.scope_id}@{item.version}" for item in failed
            )
            raise VerificationError(f"DefinitionHash mismatch: {details}")
        negative_guard_count = 0
        if args.self_test:
            negative_guard_count = run_negative_self_tests(
                d5_text,
                d5_path,
                evidence_text,
                evidence_path,
                mapping_text,
                mapping_path,
            )
    except (OSError, VerificationError) as exc:
        print(f"definition-integrity ERROR {exc}", file=sys.stderr)
        return 1

    if args.manifest:
        manifest = {
            "schema": "vnapro.scope-registry-definition-integrity-manifest.v1",
            "lifecycle": REGISTRY_LIFECYCLE,
            "definition_integrity_passed": True,
            "gate_eligible": False,
            "vendor_compatibility_passed": False,
            "vendor_address_mapping_eligible": False,
            "source_dependency_check_executed": False,
            "signature_verified": False,
            "profile_states": profile_states,
            "groups": {
                name: [
                    asdict(item)
                    | {
                        "lifecycle": REGISTRY_LIFECYCLE,
                        "definition_hash_matches": item.definition_integrity,
                        "issuer_id": None,
                    }
                    for item in values
                ]
                for name, values in groups.items()
            },
        }
        print(json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2))
    else:
        print("definition-integrity scpi-address-policy tables=2/2")
        print("definition-integrity target-dependency-policy tables=2/2")
        for name, values in groups.items():
            print(
                f"definition-integrity {name} matched={len(values)}/{len(values)}"
            )
        print("definition-integrity gate-eligible=false")
        print("definition-integrity vendor-compatibility-passed=false")
        print("definition-integrity vendor-address-mapping-eligible=false")
        print("definition-integrity source-dependency-check-executed=false")
        for state in profile_states:
            print(
                "definition-integrity vendor-profile "
                f"{state['product_profile_key_pattern']}/{state['domain']} "
                f"status={state['status']}"
            )
        if args.self_test:
            print(
                "definition-integrity negative-guards "
                f"rejected={negative_guard_count}/{negative_guard_count}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
