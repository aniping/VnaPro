<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import type { InstrumentStatus } from '../../api/types'
import type {
  CalibrationCommand,
  CalibrationPreview,
  CalibrationPreviewResponse,
  CalibrationStep,
} from '../../api/calibrationTypes'
import {
  calibrationConnection,
  calibrationMethodLabel,
  calibrationPorts,
  STANDARD_LABELS,
} from '../../domain/calibration'
import CalibrationSymbol from './CalibrationSymbol.vue'
import CalibrationPreviewDiagram from './CalibrationPreviewDiagram.vue'

const props = defineProps<{
  status: InstrumentStatus
  disabled: boolean
  canCancel?: boolean
  command: (
    command: CalibrationCommand,
    expectedRevision?: string,
  ) => Promise<InstrumentStatus | null>
  loadPreview: (
    sessionId: string,
    stepId: string,
  ) => Promise<CalibrationPreviewResponse>
}>()
const emit = defineEmits<{ close: [] }>()
const calibration = computed(() => props.status.calibration!)
const sessionId = calibration.value.sessionId
const selectedStepId = ref(calibration.value.steps[0]?.id ?? '')
const name = ref(
  `${calibrationMethodLabel(calibration.value.method)} ${calibrationPorts(calibration.value.method, calibration.value.port1, calibration.value.port2)}`,
)
const working = ref(false)
const error = ref('')
const preview = ref<CalibrationPreview | null>(null)
const previewMessage = ref('连接标准并点击对应按钮采集。')
const previewLoading = ref(false)
const selectedStep = computed(() =>
  calibration.value.steps.find((step) => step.id === selectedStepId.value),
)
const stale = computed(
  () =>
    calibration.value.sessionId !== sessionId || !calibration.value.collecting,
)
const canCancel = computed(() => props.canCancel ?? !props.disabled)
const unavailable = computed(
  () =>
    props.disabled ||
    working.value ||
    calibration.value.capturePending ||
    stale.value,
)
const complete = computed(
  () =>
    calibration.value.steps.length > 0 &&
    calibration.value.steps.every((step) => step.captured),
)
const groups = computed(() => {
  const result = new Map<string, { id: string; ports: number[]; steps: CalibrationStep[] }>()
  for (const step of calibration.value.steps) {
    const id = step.port2 ? `pair-${step.port1}-${step.port2}` : `port-${step.port1}`
    if (!result.has(id)) result.set(id, { id, ports: step.port2 ? [step.port1, step.port2] : [step.port1], steps: [] })
    result.get(id)!.steps.push(step)
  }
  return [...result.values()]
})
const capturedCount = computed(() => calibration.value.steps.filter((step) => step.captured).length)
const parameter = computed(() =>
  selectedStep.value
    ? `S${selectedStep.value.port2 || selectedStep.value.port1}${selectedStep.value.port1}`
    : `S${calibration.value.port1}${calibration.value.port1}`,
)
const calibrationLabel = computed(
  () =>
    `${calibrationMethodLabel(calibration.value.method)} ${calibrationPorts(calibration.value.method, calibration.value.port1, calibration.value.port2)}`,
)
let previewEpoch = 0
async function refreshPreview() {
  const step = selectedStep.value
  const epoch = ++previewEpoch
  preview.value = null
  if (!step?.captured || stale.value) {
    previewLoading.value = false
    previewMessage.value = '尚未采集此标准。连接后点击对应按钮。'
    return
  }
  const revision = calibration.value.revision
  previewLoading.value = true
  previewMessage.value = '读取当前标准的测量数据…'
  try {
    const result = await props.loadPreview(sessionId, step.id)
    if (
      epoch !== previewEpoch ||
      stale.value ||
      calibration.value.revision !== revision ||
      selectedStepId.value !== step.id
    )
      return
    if (
      result.sessionId !== sessionId ||
      result.stepId !== step.id ||
      result.calibrationRevision !== revision
    ) {
      previewMessage.value = '标准预览版本已变化，等待当前数据。'
      return
    }
    if (result.available) {
      if (
        result.runId !== step.runId ||
        result.frameSeq !== step.frameSeq ||
        result.parameter !== parameter.value
      ) {
        previewMessage.value = '标准预览采集身份不匹配。'
        return
      }
      preview.value = result
    } else previewMessage.value = result.reason
  } catch (failure) {
    if (epoch === previewEpoch)
      previewMessage.value =
        failure instanceof Error ? failure.message : '标准预览读取失败。'
  } finally {
    if (epoch === previewEpoch) previewLoading.value = false
  }
}
// 每次轮询都会生成新状态对象；只按标准身份值变化读取，不监听临时数组引用。
watch(
  [
    () => calibration.value.sessionId,
    () => calibration.value.collecting,
    () => calibration.value.revision,
    () => selectedStepId.value,
    () => selectedStep.value?.captured,
    () => selectedStep.value?.runId,
    () => selectedStep.value?.frameSeq,
    () => parameter.value,
  ],
  () => void refreshPreview(),
  { immediate: true },
)
onBeforeUnmount(() => {
  ++previewEpoch
})
async function capture(step: CalibrationStep) {
  if (
    unavailable.value ||
    !calibration.value.steps.some((entry) => entry.id === step.id)
  )
    return
  selectedStepId.value = step.id
  working.value = true
  error.value = ''
  try {
    const result = await props.command(
      { action: 'capture', sessionId, stepId: step.id },
      calibration.value.revision,
    )
    if (!result) error.value = '采集未完成，请检查连接和状态栏后重测。'
  } finally {
    working.value = false
  }
}
async function finish() {
  if (unavailable.value || !complete.value || !name.value.trim()) return
  working.value = true
  error.value = ''
  try {
    const result = await props.command(
      { action: 'finish', sessionId, name: name.value.trim() },
      calibration.value.revision,
    )
    if (result && !result.calibration?.collecting) emit('close')
    else error.value = 'Apply 未完成，请检查标准测量，必要时重测。'
  } finally {
    working.value = false
  }
}
async function cancel() {
  if (working.value || calibration.value.capturePending) return
  if (!canCancel.value || stale.value) {
    emit('close')
    return
  }
  working.value = true
  try {
    const result = await props.command(
      { action: 'cancel', sessionId },
      calibration.value.revision,
    )
    if (result) emit('close')
    else error.value = '取消尚未确认，校准会话仍由服务端保留。'
  } finally {
    working.value = false
  }
}
function standardLetter(kind: CalibrationStep['kind']) {
  return { open: 'O', short: 'S', load: 'M', thru: 'T', isolation: 'I' }[kind]
}
function portComplete(port: number) {
  return calibration.value.steps
    .filter((step) => step.port1 === port && step.port2 === 0)
    .every((step) => step.captured)
}
</script>

<template>
  <section
    class="cal-acquisition"
    :class="{ 'is-four-port': calibration.method === 'fourPort' }"
    aria-label="Calibration measurement workspace"
    :aria-busy="working || calibration.capturePending"
  >
    <div class="cal-acquisition-diagrams">
      <CalibrationPreviewDiagram
        v-for="format in ['db', 'phase'] as const"
        :key="format"
        :format="format"
        :channel-id="status.activeChannelId"
        :preview="preview"
        :parameter="parameter"
        :config="status.config"
        :calibration-label="calibrationLabel"
        :empty-text="
          previewLoading ? '读取当前标准的测量数据…' : previewMessage
        "
      />
    </div>
    <section class="cal-acquisition-dock" aria-label="Calibration standards">
      <div class="cal-dock-rail">Calibration - Ch{{ status.activeChannelId ?? 1 }}</div>
      <div class="cal-dock-body">
        <div class="cal-standard-groups">
          <section
            v-for="group in groups"
            :key="group.id"
            class="cal-standard-group"
            :data-cal-group="group.id"
            :aria-label="group.ports.map((port) => `P${port}`).join(' to ')"
          >
            <div
              class="cal-standard-port"
              :class="{
                selected: group.steps.some(
                  (step) => step.id === selectedStepId,
                ),
              }"
            >
              <div
                v-if="group.ports.length === 1"
                class="cal-standard-port-line"
              >
                <strong>P{{ group.ports[0] }}</strong
                ><span class="cal-port-ring"
                  ><CalibrationSymbol kind="port" /></span
                ><span
                  v-if="portComplete(group.ports[0]!)"
                  class="cal-port-complete"
                  ><CalibrationSymbol kind="check"
                /></span>
              </div>
              <div v-else class="cal-pair-topology">
                <strong>P{{ group.ports[0] }}</strong
                ><span class="cal-port-ring"
                  ><CalibrationSymbol kind="port" /></span
                ><span class="cal-thru-wire" /><span class="cal-port-ring"
                  ><CalibrationSymbol kind="port" /></span
                ><strong>P{{ group.ports[1] }}</strong>
              </div>
              <small>50 Ω (ideal)<br />Ideal 50 Ω Kit</small>
            </div>
            <button
              v-for="step in group.steps"
              :key="step.id"
              type="button"
              class="cal-standard-button"
              :class="{
                captured: step.captured,
                measuring: selectedStepId === step.id && working,
              }"
              :disabled="unavailable"
              :title="calibrationConnection(step, calibration.method)"
              :data-cal-step="step.id"
              @click="capture(step)"
            >
              <span class="cal-standard-letter">{{
                standardLetter(step.kind)
              }}</span
              ><span
                >{{ STANDARD_LABELS[step.kind]
                }}<small>{{
                  selectedStepId === step.id && working ? '采集中…' : '(ideal)'
                }}</small></span
              ><span
                v-if="step.captured"
                class="cal-standard-check"
                role="img"
                aria-label="Captured"
                ><CalibrationSymbol kind="check"
              /></span></button
            ><button
              v-if="group.ports.length === 1"
              type="button"
              class="cal-standard-button unsupported"
              disabled
              title="理想校准件未提供Sliding Match"
            >
              <span class="cal-standard-letter sliding">M</span
              ><span>Sliding Match<small>Not available</small></span></button
            ><button
              v-else-if="!calibration.includeIsolation"
              type="button"
              class="cal-standard-button unsupported"
              disabled
              title="未选择隔离采集；串扰假定为零"
            >
              <span class="cal-standard-letter">I</span
              ><span>Isolation<small>Not selected</small></span>
            </button>
          </section>
        </div>
        <div class="cal-dock-summary">
          <p v-if="calibration.method === 'fourPort'" class="cal-four-port-progress" role="status">完整四端口 TOSM · {{ capturedCount }} / {{ calibration.steps.length }} 项已采集</p>
          <button
            v-if="
              selectedStep?.captured && !preview && !previewLoading && !stale
            "
            type="button"
            class="cal-preview-retry"
            @click="refreshPreview"
          >
            重新读取预览
          </button>
          <label
            >Name<input
              v-model="name"
              maxlength="128"
              :disabled="unavailable"
              aria-label="Calibration Name"
          /></label>
          <p>
            {{
              selectedStep
                ? calibrationConnection(selectedStep, calibration.method)
                : '连接标准并选择对应按钮。'
            }}
          </p>
          <p v-if="preview" class="cal-preview-identity">
            {{ preview.parameter }} · Run {{ preview.runId }} / Frame
            {{ preview.frameSeq }} · RAW standard
          </p>
          <p
            v-if="
              calibration.method !== 'onePort' && !calibration.includeIsolation
            "
            class="cal-assumption"
          >
            未测 Isolation；假定串扰为零。
          </p>
          <p v-if="calibration.method === 'fourPort'">P1–P4 各测三项 OSM；全部六对直接测量 Through{{ calibration.includeIsolation ? ' 与 Isolation' : '' }}。向下滚动可访问所有端口对。</p>
          <p
            v-if="stale || error || calibration.error"
            class="cal-dock-error"
            role="alert"
          >
            {{
              stale
                ? '校准会话已变化，请关闭后重新打开。'
                : error || calibration.error
            }}
          </p>
          <p
            v-if="!canCancel && !working && !calibration.capturePending"
            class="cal-assumption"
          >
            当前只读；Close 仅关闭界面，会话保留。
          </p>
        </div>
      </div>
      <footer class="cal-dock-footer">
        <button
          type="button"
          class="cal-dock-action cal-abort"
          disabled
          title="本版不能中止已受理的采集；完成后可取消会话"
        >
          <CalibrationSymbol kind="cancel" />Abort</button
        ><button
          type="button"
          class="cal-dock-toggle"
          disabled
          title="每个标准固定从新扫描起点开始"
        >
          Restart Sweep on Std.<br />Meas.<span class="cal-toggle-state on"
            >On</span
          ></button
        ><button
          type="button"
          class="cal-dock-toggle"
          disabled
          title="当前只支持完整单次校准扫描"
        >
          Continuous Cal<br />Sweeps<span class="cal-toggle-state"
            >Off</span
          ></button
        ><button
          type="button"
          class="cal-dock-action"
          :disabled="unavailable || !complete || !name.trim()"
          @click="finish"
        >
          <CalibrationSymbol kind="check" />Apply</button
        ><button
          type="button"
          class="cal-dock-action"
          :disabled="working || calibration.capturePending"
          @click="cancel"
        >
          <CalibrationSymbol kind="cancel" />{{
            canCancel ? 'Cancel' : 'Close'
          }}
        </button>
      </footer>
    </section>
  </section>
</template>

<style scoped>
.cal-acquisition {
  display: grid;
  grid-template-rows: minmax(0, 0.9fr) minmax(270px, 1fr);
  min-width: 0;
  min-height: 0;
  background: #101619;
  color: #eaf1f5;
  font:
    12px Tahoma,
    Arial,
    sans-serif;
}
.cal-acquisition-diagrams {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 2px;
  min-height: 0;
}
.cal-acquisition-dock {
  display: grid;
  grid-template-columns: 27px minmax(0, 1fr);
  grid-template-rows: minmax(0, 1fr) 48px;
  min-height: 0;
  border: 1px solid #435862;
}
.cal-dock-rail {
  grid-column: 1;
  grid-row: 1 / 3;
  padding: 7px 5px;
  writing-mode: vertical-rl;
  transform: rotate(180deg);
  background: #2f414b;
  font-size: 11px;
}
.cal-dock-body {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  min-height: 0;
  padding: 4px 4px 0;
  overflow: auto;
}
.cal-standard-groups {
  display: flex;
  flex: 0 0 auto;
  gap: 5px;
}
.cal-standard-group {
  width: 131px;
}
.is-four-port .cal-dock-body {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 180px;
}
.is-four-port .cal-standard-groups {
  display: grid;
  grid-template-columns: repeat(4, minmax(110px, 1fr));
  min-width: 0;
}
.is-four-port .cal-standard-group {
  width: auto;
}
.is-four-port .cal-dock-summary {
  position: sticky;
  top: 0;
}
.cal-four-port-progress {
  margin-top: 0;
  color: #edf5f8;
  font-size: 11px;
}
.cal-standard-port {
  min-height: 73px;
  margin-bottom: 3px;
  padding: 3px 2px;
  border: 2px solid transparent;
}
.cal-standard-port.selected {
  border-color: #009ff5;
}
.cal-standard-port-line {
  display: flex;
  align-items: center;
  gap: 5px;
  height: 23px;
}
.cal-standard-port strong {
  font-size: 16px;
  font-weight: 600;
}
.cal-port-ring {
  display: block;
  width: 24px;
  height: 24px;
  margin-left: auto;
  color: #edf4f6;
  --port-fill: #101619;
}
.cal-port-complete {
  width: 15px;
  height: 15px;
  color: #59d16e;
}
.cal-standard-port small {
  display: block;
  margin-top: 3px;
  color: #d7e5ed;
  font-size: 11px;
  line-height: 15px;
}
.cal-pair-topology {
  display: flex;
  align-items: center;
  height: 23px;
  gap: 2px;
  white-space: nowrap;
}
.cal-pair-topology strong {
  font-size: 14px;
}
.cal-pair-topology .cal-port-ring {
  width: 20px;
  height: 20px;
  margin: 0;
}
.cal-thru-wire {
  width: 17px;
  height: 3px;
  background: #d9e5eb;
}
.cal-standard-button {
  position: relative;
  display: flex;
  align-items: center;
  width: 100%;
  min-height: 45px;
  margin-bottom: 2px;
  padding: 4px 17px 4px 4px;
  border: 0;
  color: #f0f5f8;
  background: #4a616e;
  text-align: left;
  font-size: 12px;
  line-height: 14px;
}
.cal-standard-button small {
  display: block;
  font-size: 10px;
  line-height: 13px;
}
.cal-standard-letter {
  display: grid;
  place-items: center;
  width: 21px;
  height: 21px;
  flex: 0 0 21px;
  margin-right: 4px;
  border-radius: 50%;
  color: #f2f7fa;
  background: #288fe5;
  font-size: 17px;
  line-height: 21px;
}
.cal-standard-button:hover:not(:disabled),
.cal-standard-button.measuring {
  background: #0c86bd;
}
.cal-standard-button:disabled {
  color: #95a8b3;
  background: #344a57;
}
.cal-standard-button.unsupported .cal-standard-letter {
  background: #4c7188;
}
.cal-standard-check {
  position: absolute;
  top: 6px;
  right: 3px;
  width: 14px;
  height: 14px;
  color: #58dc75;
}
.cal-dock-summary {
  flex: 1;
  min-width: 105px;
  padding: 6px 3px;
  color: #b9cbd5;
  font-size: 10px;
  line-height: 1.5;
}
.cal-dock-summary label {
  display: flex;
  align-items: center;
  gap: 5px;
}
.cal-preview-retry {
  margin-bottom: 5px;
  border: 1px solid #536d7c;
  padding: 3px 6px;
  color: #eaf1f5;
  background: #405663;
  font-size: 10px;
}
.cal-dock-summary input {
  width: 100%;
  min-width: 0;
  height: 24px;
  padding: 3px;
  border: 1px solid #536d7c;
  color: #eef4f8;
  background: #25343e;
  font-size: 10px;
}
.cal-preview-identity {
  font-variant-numeric: tabular-nums;
}
.cal-assumption,
.cal-dock-error {
  color: #ffd17c;
}
.cal-dock-footer {
  display: flex;
  grid-column: 2;
  gap: 4px;
  padding: 4px;
  background: #2f414b;
}
.cal-dock-action {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  min-width: 82px;
  padding: 3px 8px;
  border: 0;
  color: #edf5f8;
  background: #4a616e;
  font-size: 12px;
}
.cal-dock-action > svg {
  width: 23px;
  height: 23px;
  color: #adb9c0;
}
.cal-dock-action:last-child > svg {
  color: #ff3530;
}
.cal-dock-action:disabled {
  color: #81939e;
  background: #3b4448;
}
.cal-abort {
  min-width: 100px;
}
.cal-dock-toggle {
  position: relative;
  flex: 1;
  min-width: 130px;
  padding: 2px 37px 2px 4px;
  border: 0;
  color: #edf5f8;
  background: #26353d;
  text-align: left;
  font-size: 10px;
  line-height: 13px;
}
.cal-toggle-state {
  position: absolute;
  right: 9px;
  bottom: 9px;
  min-width: 23px;
  padding: 0 2px;
  color: #bac8d0;
  background: #526b79;
  text-align: center;
}
.cal-toggle-state.on {
  color: #fff;
  background: #009af0;
}
@media (max-width: 850px) {
  .is-four-port .cal-dock-body {
    grid-template-columns: minmax(0, 1fr);
  }
  .is-four-port .cal-standard-groups {
    grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
  }
  .is-four-port .cal-dock-summary {
    position: static;
  }
}
</style>
