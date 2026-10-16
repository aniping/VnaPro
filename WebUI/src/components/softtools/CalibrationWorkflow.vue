<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import type { InstrumentStatus } from '../../api/types'
import type {
  CalibrationCommand,
  CalibrationMethod,
} from '../../api/calibrationTypes'
import CalibrationSymbol from './CalibrationSymbol.vue'

const props = defineProps<{
  status: InstrumentStatus
  disabled: boolean
  command: (
    command: CalibrationCommand,
    expectedRevision?: string,
  ) => Promise<InstrumentStatus | null>
}>()
const emit = defineEmits<{ close: [] }>()
const dialog = ref<HTMLDialogElement | null>(null)
const phase = ref(1)
const ports = computed(() =>
  Array.from({ length: props.status.capabilities.portCount }, (_, i) => i + 1),
)
const selectedPorts = ref(ports.value.length > 1 ? [1, 2] : [1])
const method = ref<CalibrationMethod>(
  ports.value.length > 1 ? 'twoPort' : 'onePort',
)
const includeIsolation = ref(false)
const working = ref(false)
const error = ref('')
const setupRevision = props.status.acquisitionRevision
const stale = computed(() => props.status.acquisitionRevision !== setupRevision)
const unavailable = computed(
  () => props.disabled || working.value || stale.value,
)
const validSetup = computed(
  () => method.value === 'fourPort'
    ? ports.value.length >= 4 && [1, 2, 3, 4].every((port) => selectedPorts.value.includes(port)) && selectedPorts.value.length === 4
    : selectedPorts.value.length === (method.value === 'onePort' ? 1 : 2),
)
const methodName = computed(() =>
  method.value === 'onePort' ? 'Refl OSM' : method.value === 'fourPort' ? 'TOSM · 4-Port' : 'TOSM · 2-Port',
)
const selectedNames = computed(() =>
  selectedPorts.value.map((port) => `P${port}`).join(', '),
)
const types: {
  id: string
  label: string
  symbol: 'reflection' | 'transmission' | 'both' | 'onePath' | 'full'
  method?: CalibrationMethod
}[] = [
  { id: 'refl-open', label: 'Refl\nNorm\nOpen', symbol: 'reflection' },
  { id: 'refl-short', label: 'Refl\nNorm\nShort', symbol: 'reflection' },
  { id: 'osm', label: 'Refl OSM', symbol: 'reflection', method: 'onePort' },
  { id: 'trans', label: 'Trans\nNorm', symbol: 'transmission' },
  { id: 'trans-both', label: 'Trans\nNorm\nBoth', symbol: 'both' },
  { id: 'one-path', label: 'One Path\nTwo\nPorts', symbol: 'onePath' },
  { id: 'tosm', label: 'TOSM', symbol: 'full', method: 'twoPort' },
  { id: 'tosm-four-port', label: 'TOSM\n4 Ports', symbol: 'full', method: 'fourPort' },
  { id: 'uosm', label: 'UOSM', symbol: 'full' },
  { id: 'trl', label: 'TRL', symbol: 'full' },
  { id: 'nist', label: 'NIST\nMultiline\nTRL', symbol: 'full' },
  { id: 'tom', label: 'TOM', symbol: 'full' },
  { id: 'tsm', label: 'TSM', symbol: 'full' },
  { id: 'trm', label: 'TRM', symbol: 'full' },
  { id: 'tna', label: 'TNA', symbol: 'full' },
  { id: 'adapter', label: 'Adapter\nRemoval', symbol: 'full' },
  { id: 'huosm', label: 'HUOSM', symbol: 'full' },
]
function selectPort(port: number) {
  if (unavailable.value || method.value === 'fourPort' || !ports.value.includes(port)) return
  selectedPorts.value = selectedPorts.value.includes(port)
    ? selectedPorts.value.filter((entry) => entry !== port)
    : [...selectedPorts.value, port].sort((first, second) => first - second)
  error.value = ''
}
function selectMethod(value: CalibrationMethod) {
  if (unavailable.value || (value === 'fourPort' && ports.value.length < 4)) return
  if (value === 'fourPort') selectedPorts.value = [1, 2, 3, 4]
  else if (method.value === 'fourPort') selectedPorts.value = value === 'onePort' ? [1] : [1, 2]
  method.value = value
}
function next() {
  if (!unavailable.value && validSetup.value) phase.value = 2
}
async function begin() {
  const calibration = props.status.calibration
  if (unavailable.value || !validSetup.value || !calibration) return
  working.value = true
  error.value = ''
  try {
    const result = await props.command(
      {
        action: 'begin',
        method: method.value,
        port1: selectedPorts.value[0]!,
        ...(method.value !== 'onePort'
          ? {
              port2: method.value === 'fourPort' ? 4 : selectedPorts.value[1]!,
              includeIsolation: includeIsolation.value,
            }
          : {}),
      },
      calibration.revision,
    )
    if (!result?.calibration?.collecting)
      error.value = '校准未开始，请检查仪器状态栏。'
    // 会话发布后由 App 切换至采集工作区，不建立第三步弹框。
  } finally {
    working.value = false
  }
}
function close() {
  if (!working.value) emit('close')
}
onMounted(() => dialog.value?.showModal())
</script>

<template>
  <dialog
    ref="dialog"
    class="cal-setting"
    aria-labelledby="calibration-title"
    :aria-busy="working"
    @cancel.prevent="close"
  >
    <header class="cal-window-title">
      <h2 id="calibration-title">Calibration Setting</h2>
      <div class="cal-title-tools">
        <button
          type="button"
          disabled
          title="帮助尚未接入"
          aria-label="Calibration help unavailable"
        >
          ?</button
        ><button
          type="button"
          :disabled="working"
          aria-label="Close calibration setting"
          @click="close"
        >
          ×
        </button>
      </div>
    </header>
    <div class="cal-setting-body">
      <div class="cal-setting-steps" aria-label="Calibration setting steps">
        <span class="cal-step-line" :class="{ advanced: phase === 2 }" />
        <div
          class="cal-step"
          :class="{ current: phase === 1, completed: phase === 2 }"
        >
          <span>1</span><small>Ports and Type</small>
        </div>
        <div class="cal-step" :class="{ current: phase === 2 }">
          <span>2</span><small>Connectors and Cal Kits</small>
        </div>
      </div>
      <div class="cal-summary">
        <div>Calibrations</div>
        <p>
          <span>{{ methodName }}</span
          ><span>{{ selectedNames || 'No ports selected' }}</span>
        </p>
      </div>
      <template v-if="phase === 1">
        <h3 class="cal-section-heading">Configuration</h3>
        <div
          class="cal-port-topology"
          role="group"
          aria-label="Ports to calibrate"
        >
          <div v-for="port in ports" :key="port" class="cal-port-branch">
            <button
              type="button"
              class="cal-port-tile"
              :class="{ selected: selectedPorts.includes(port) }"
              :aria-label="`Port ${port}`"
              :aria-pressed="selectedPorts.includes(port)"
              :disabled="unavailable || method === 'fourPort'"
              :title="method === 'fourPort' ? '完整四端口固定使用 P1、P2、P3、P4' : undefined"
              :data-cal-port="port"
              @click="selectPort(port)"
            >
              <span>P{{ port }}</span
              ><CalibrationSymbol kind="port" /><span
                class="cal-port-check"
                :class="{ checked: selectedPorts.includes(port) }"
                ><CalibrationSymbol
                  v-if="selectedPorts.includes(port)"
                  kind="check"
              /></span></button
            ><span class="cal-port-wire" />
          </div>
        </div>
        <h3 class="cal-section-heading">Type</h3>
        <div class="cal-type-matrix" role="group" aria-label="Calibration type">
          <button
            v-for="type in types"
            :key="type.id"
            type="button"
            class="cal-type"
            :class="{ selected: type.method === method }"
            :disabled="unavailable || !type.method || (type.method === 'fourPort' && ports.length < 4)"
            :aria-label="type.label.replaceAll('\n', ' ')"
            :aria-pressed="type.method === method"
            :title="
              type.method
                ? type.method === 'fourPort'
                  ? ports.length < 4 ? '需要四个物理端口；当前设备不支持' : '四个端口 · 全六对直接测量 · 理想 50 Ω 标准'
                  : `${type.method === 'onePort' ? '一个端口' : '两个端口'} · 理想 50 Ω 标准`
                : '尚未实现此校准方法'
            "
            :data-cal-type="type.id"
            @click="type.method && selectMethod(type.method)"
          >
            <CalibrationSymbol :kind="type.symbol" /><span>{{
              type.label
            }}</span>
          </button>
        </div>
        <div class="cal-source-disabled">
          <span>Source</span><CalibrationSymbol kind="unavailable" />
        </div>
        <p class="cal-setting-notice" :class="{ error: !validSetup }">
          {{
            validSetup
              ? method === 'fourPort'
                ? '完整 P1–P4：各端口 Open / Short / Match，共 12 项；全部六对 Through 直接测量，共 18 项。'
                : '提供单端口 Refl OSM、双端口 TOSM，以及四端口设备的完整 TOSM。'
              : `${methodName} 需要选择${method === 'onePort' ? '一个' : method === 'fourPort' ? '四个' : '两个'}物理端口。`
          }}
        </p>
      </template>
      <template v-else>
        <h3 class="cal-section-heading">Configuration</h3>
        <div
          class="cal-connector-matrix"
          :style="{
            gridTemplateColumns: selectedPorts.length === 4 ? '100px repeat(4, minmax(0, 1fr))' : `124px repeat(${selectedPorts.length}, 136px)`,
            '--cal-port-count': selectedPorts.length,
          }"
        >
          <span />
          <div
            v-for="port in selectedPorts"
            :key="`port-${port}`"
            class="cal-connector-branch"
          >
            <div class="cal-port-tile selected">
              <span>P{{ port }}</span
              ><CalibrationSymbol kind="port" />
            </div>
            <span class="cal-port-wire" />
          </div>
          <template
            v-for="row in [
              { label: 'Connector', value: '50 Ω (ideal)' },
              { label: 'Port Gender', value: 'Not specified' },
              { label: 'Cal Kit', value: 'Ideal 50 Ω Kit' },
              { label: 'Kit Modified', value: '' },
            ]"
            :key="row.label"
            ><span class="cal-connector-label">{{ row.label }}</span>
            <div
              v-for="port in selectedPorts"
              :key="`${row.label}-${port}`"
              class="cal-connector-value"
              :class="{ modified: row.label === 'Kit Modified' }"
              aria-disabled="true"
              :aria-label="`${row.label}, P${port}, ${row.value || 'Not modified'}, read only`"
              title="固定理想模型，仅显示；本版不能修改"
            >
              <span>{{ row.value }}</span
              ><span
                v-if="row.label !== 'Kit Modified'"
                class="cal-field-arrow"
                aria-hidden="true"
              /></div
          ></template>
        </div>
        <h3 class="cal-section-heading cal-additional-heading">
          Additional Settings
        </h3>
        <div class="cal-additional">
          <button type="button" disabled title="理想50Ω模型固定相同连接器阻抗">
            Same Connector<br />all Ports<span class="cal-checkbox checked"
              ><CalibrationSymbol kind="check"
            /></span></button
          ><button type="button" disabled title="未接入机械连接器性别模型">
            Same Gender all<br />Ports<span class="cal-checkbox" /></button
          ><button type="button" disabled title="尚未接入非理想校准件文件">
            Import Cal Kit<span class="cal-next-small"
              ><CalibrationSymbol kind="next"
            /></span>
          </button>
        </div>
        <label v-if="method !== 'onePort'" class="cal-isolation-setting"
          ><input
            v-model="includeIsolation"
            type="checkbox"
            :disabled="unavailable"
          />Measure Isolation{{ method === 'fourPort' ? ' · 全六对，合计 24 项标准' : '' }}</label
        >
        <p class="cal-setting-notice">
          理想 50 Ω：Open +1、Short −1、Match 0；{{
            method !== 'onePort'
              ? includeIsolation
                ? '采集真实隔离响应。'
                : 'Through 为理想直通，未测隔离时假定串扰为零。'
              : '机械连接器与性别模型未接入。'
          }}
        </p>
        <p v-if="method === 'fourPort'" class="cal-setting-notice">完整四端口 TOSM：12 项 OSM + 6 对 Through{{ includeIsolation ? ' + 6 对 Isolation' : '' }}。各对均直接测量；Through 假定双向互易、无损且匹配。未参与连接的端口均接 50 Ω 匹配负载。当前不支持未知直通或非理想件模型。</p>
      </template>
      <p v-if="stale || error" class="cal-setting-notice error" role="alert">
        {{ stale ? '扫描设置已变化，请关闭并重新打开。' : error }}
      </p>
    </div>
    <footer class="cal-setting-footer">
      <button
        v-if="phase === 2"
        type="button"
        class="cal-action"
        :disabled="unavailable"
        @click="phase = 1"
      >
        <CalibrationSymbol kind="previous" />Previous</button
      ><button
        v-if="phase === 1"
        type="button"
        class="cal-action"
        :disabled="unavailable || !validSetup"
        @click="next"
      >
        <CalibrationSymbol kind="next" />Next</button
      ><button
        v-else
        type="button"
        class="cal-action"
        :disabled="unavailable || !validSetup"
        @click="begin"
      >
        <CalibrationSymbol kind="start" />{{
          working ? 'Starting…' : 'Start'
        }}</button
      ><button
        type="button"
        class="cal-action"
        :disabled="working"
        @click="close"
      >
        <CalibrationSymbol kind="cancel" />Cancel
      </button>
    </footer>
  </dialog>
</template>

<style scoped>
.cal-setting {
  width: 654px;
  height: 552px;
  max-width: calc(100vw - 16px);
  max-height: calc(100vh - 16px);
  display: none;
  grid-template-rows: 26px minmax(0, 1fr) 38px;
  padding: 0;
  border: 2px solid #33434b;
  color: #f3f5f7;
  background: #101619;
  font:
    11px Tahoma,
    Arial,
    sans-serif;
}
.cal-setting[open] {
  display: grid;
}
.cal-setting::backdrop {
  background: #0005;
}
.cal-window-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 5px;
  background: #29363e;
}
.cal-window-title h2 {
  margin: 0;
  font-size: 11px;
  font-weight: 400;
}
.cal-title-tools {
  display: flex;
  align-items: center;
  gap: 8px;
}
.cal-title-tools button {
  width: 20px;
  height: 22px;
  padding: 0;
  border: 0;
  background: transparent;
  color: inherit;
  font-size: 17px;
}
.cal-setting-body {
  min-height: 0;
  padding: 0 9px;
  overflow: auto;
}
.cal-setting-steps {
  position: relative;
  display: flex;
  justify-content: space-between;
  height: 80px;
  margin: 0 119px;
  padding-top: 20px;
}
.cal-step-line {
  position: absolute;
  left: 12px;
  right: 12px;
  top: 32px;
  height: 1px;
  background: #8a9397;
}
.cal-step-line.advanced {
  background: #00a0f0;
}
.cal-step {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  width: 24px;
  color: #9aa5aa;
}
.cal-step > span {
  display: grid;
  place-items: center;
  width: 24px;
  height: 24px;
  border: 2px solid currentColor;
  border-radius: 50%;
  background: #101619;
  font-size: 12px;
}
.cal-step small {
  position: absolute;
  top: 33px;
  width: 155px;
  text-align: center;
  white-space: nowrap;
  font-size: 10px;
}
.cal-step.current {
  color: #fff;
}
.cal-step.current > span {
  background: #009ff5;
  border-color: #009ff5;
}
.cal-step.completed > span {
  color: #fff;
  border-color: #009ff5;
}
.cal-step.completed small {
  color: #fff;
}
.cal-summary {
  height: 37px;
  margin-bottom: 8px;
  border: 1px solid #33464f;
  background: #202b31;
}
.cal-summary > div {
  height: 18px;
  padding: 2px 5px;
  background: #33434b;
}
.cal-summary p {
  display: flex;
  justify-content: space-between;
  margin: 0;
  padding: 1px 4px 1px 105px;
  font-size: 10px;
}
.cal-section-heading {
  height: 17px;
  margin: 0;
  border-bottom: 1px solid #485d68;
  font-size: 11px;
  font-weight: 400;
  line-height: 16px;
}
.cal-port-topology {
  display: flex;
  justify-content: center;
  gap: 5px;
  min-height: 77px;
  padding-top: 4px;
}
.cal-port-branch {
  width: 116px;
}
.cal-port-tile {
  position: relative;
  display: flex;
  align-items: center;
  width: 100%;
  height: 35px;
  padding: 0 7px;
  border: 0;
  color: #f5f8fa;
  background: #495f6b;
  --port-fill: #495f6b;
  font-size: 11px;
}
.cal-port-tile.selected {
  background: #0099f0;
  --port-fill: #0099f0;
}
.cal-port-tile > svg {
  position: absolute;
  left: calc(50% - 11px);
  width: 22px;
  height: 22px;
}
.cal-port-tile:disabled {
  opacity: 0.7;
}
.cal-port-check {
  position: absolute;
  right: 5px;
  top: 5px;
  width: 14px;
  height: 14px;
  border: 1px solid #a4bdca;
}
.cal-port-check.checked {
  color: #b0d6ea;
}
.cal-port-wire {
  display: block;
  width: 1px;
  height: 19px;
  margin: 5px auto 0;
  border-left: 2px dashed #d9e2e6;
}
.cal-type-matrix {
  display: grid;
  grid-template-columns: repeat(11, minmax(0, 1fr));
  grid-auto-rows: 83px;
  min-height: 187px;
  padding: 5px 3px;
  border: 1px solid #33464f;
  background: #202b31;
}
.cal-type {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 0;
  padding: 3px 1px;
  border: 0;
  color: #f0f5f7;
  background: transparent;
  font-size: 10px;
  line-height: 12px;
}
.cal-type > svg {
  width: 36px;
  height: 40px;
  margin-bottom: 4px;
  color: #50bd0a;
}
.cal-type > span {
  white-space: pre-line;
  text-align: center;
}
.cal-type.selected {
  color: #11191d;
  background: #f4f6f7;
}
.cal-type:disabled {
  color: #97a5ad;
}
.cal-type:disabled > svg {
  color: #6b8270;
}
.cal-source-disabled {
  display: flex;
  justify-content: space-between;
  width: 130px;
  height: 30px;
  margin-top: 4px;
  padding: 3px;
  color: #99a1a5;
  background: #343536;
}
.cal-source-disabled > svg {
  width: 16px;
  height: 16px;
}
.cal-setting-notice {
  margin: 5px 0;
  color: #b9c6cd;
  font-size: 10px;
  line-height: 1.45;
}
.cal-setting-notice.error {
  color: #ffcf77;
}
.cal-connector-matrix {
  display: grid;
  align-items: center;
  gap: 5px;
  margin: 4px 0 0 16px;
}
.cal-connector-label {
  padding-right: 17px;
  text-align: right;
}
.cal-connector-value {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  height: 34px;
  padding-left: 5px;
  color: #eff5f8;
  background: #202b31;
}
.cal-connector-value > span:first-child {
  padding: 4px;
}
.cal-connector-value.modified {
  background: #343536;
}
.cal-field-arrow {
  position: relative;
  align-self: stretch;
  width: 18px;
  flex: 0 0 18px;
  background: #405663;
}
.cal-field-arrow::after {
  position: absolute;
  top: 14px;
  left: 5px;
  width: 0;
  height: 0;
  border-left: 4px solid transparent;
  border-right: 4px solid transparent;
  border-top: 5px solid #dfeaf0;
  content: '';
}
.cal-additional-heading {
  margin-top: 8px;
}
.cal-additional {
  display: flex;
  gap: 5px;
  margin-top: 4px;
}
.cal-additional button {
  position: relative;
  width: 138px;
  height: 34px;
  padding: 2px 29px 2px 4px;
  border: 0;
  color: #c3d0d8;
  background: #405663;
  text-align: left;
  font-size: 10px;
  line-height: 13px;
}
.cal-checkbox {
  position: absolute;
  top: 9px;
  right: 7px;
  width: 18px;
  height: 18px;
  border: 1px solid #a8b8c2;
}
.cal-checkbox.checked {
  color: #18a6f5;
}
.cal-next-small {
  position: absolute;
  right: 5px;
  top: 10px;
  width: 12px;
  height: 12px;
}
.cal-isolation-setting {
  display: flex;
  align-items: center;
  gap: 5px;
  margin-top: 8px;
  font-size: 10px;
}
.cal-isolation-setting input {
  margin: 0;
  accent-color: #0099f0;
}
.cal-setting-footer {
  display: flex;
  justify-content: flex-end;
  align-items: center;
  gap: 4px;
  padding: 3px 5px;
  background: #29363e;
}
.cal-action {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  width: 90px;
  height: 32px;
  padding: 3px;
  border: 0;
  color: #edf5f8;
  background: #405663;
  font-size: 11px;
}
.cal-action > svg {
  width: 18px;
  height: 18px;
  flex-shrink: 0;
}
.cal-action:last-child > svg {
  color: #ff3832;
}
.cal-action:disabled {
  color: #889aa4;
  background: #303d44;
}
.cal-action:hover:not(:disabled) {
  background: #547080;
}
@media (max-width: 600px) {
  .cal-setting {
    height: auto;
    min-height: 500px;
  }
  .cal-setting-steps {
    margin-left: 70px;
    margin-right: 70px;
  }
  .cal-port-branch {
    width: 95px;
  }
  .cal-type-matrix {
    grid-template-columns: repeat(8, minmax(0, 1fr));
  }
  .cal-connector-matrix {
    margin-left: 0;
    grid-template-columns: 85px repeat(var(--cal-port-count), minmax(0, 1fr)) !important;
  }
  .cal-connector-value {
    font-size: 10px;
  }
  .cal-additional button {
    flex: 1;
    min-width: 0;
  }
}
</style>
