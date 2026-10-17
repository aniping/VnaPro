<script setup lang="ts">
import { computed, nextTick, reactive, ref, watch } from 'vue'
import type {
  InstrumentStatus,
  SweepConfiguration,
  SweepPatch,
} from '../../api/types'
import { sweepWithinCapabilities } from '../../domain/sweep'
import { frequencyText } from '../../plot/formatting'
import FrequencyField from '../shared/FrequencyField.vue'
import SweepTimeFields from '../shared/SweepTimeFields.vue'

const props = defineProps<{
  status: InstrumentStatus | null
  mode: 'frequency' | 'points' | 'bandwidth' | 'power'
  canConfigure: boolean
  busy: boolean
  focusField?: 'start' | 'stop' | 'center' | 'span'
  apply: (
    candidate: SweepPatch,
    expectedRevision: string,
    channelId?: number,
  ) => Promise<InstrumentStatus | null>
}>()

const draft = reactive<SweepConfiguration>({
  startHz: 0,
  stopHz: 0,
  points: 0,
  ifBandwidthHz: 0,
})
const dirty = ref(false)
const applying = ref(false)
const editRevision = ref('')
const editChannelId = ref<number | undefined>()
const validation = ref('')
const frequencyMode = ref<'edges' | 'centerSpan'>('edges')
const centerDraft = ref(0)
const spanDraft = ref(0)
let baseline: SweepConfiguration | null = null

function configuration(status: InstrumentStatus): SweepConfiguration {
  const { startHz, stopHz, points, ifBandwidthHz, powerDbm, sweepTimeAuto, sweepTimeSeconds } = status.config
  return { startHz, stopHz, points, ifBandwidthHz, powerDbm, sweepTimeAuto, sweepTimeSeconds }
}

function signature(value: SweepConfiguration): string {
  if (props.mode === 'points') return JSON.stringify([value.points, value.sweepTimeAuto, value.sweepTimeSeconds])
  if (props.mode === 'bandwidth') return JSON.stringify(value.ifBandwidthHz)
  if (props.mode === 'power') return JSON.stringify(value.powerDbm)
  return JSON.stringify([value.startHz, value.stopHz, value.powerDbm])
}

function load(status: InstrumentStatus) {
  baseline = configuration(status)
  Object.assign(draft, baseline)
  editRevision.value = status.revision
  editChannelId.value = status.activeChannelId
  dirty.value = false
  validation.value = ''
  frequencyMode.value = 'edges'
  centerDraft.value = (status.config.startHz + status.config.stopHz) / 2
  spanDraft.value = status.config.stopHz - status.config.startHz
}

watch(
  () => props.status?.activeChannelId,
  () => { if (props.status && !applying.value) load(props.status) },
)

watch(
  () => props.status,
  (status) => {
    if (status && !dirty.value && !applying.value) load(status)
  },
  { immediate: true },
)

watch(
  () => props.focusField,
  async (field) => {
    if (!field) return
    await nextTick()
    const input = document.getElementById(`stimulus-${field}`)
    if (input instanceof HTMLInputElement) {
      input.focus()
      input.select()
    } else input?.focus()
  },
  { immediate: true },
)

function update() {
  dirty.value =
    baseline === null ||
    signature(previewConfiguration()) !== signature(baseline)
  validation.value = ''
}

const centerHz = computed(() =>
  frequencyMode.value === 'centerSpan'
    ? centerDraft.value
    : (draft.startHz + draft.stopHz) / 2,
)
const spanHz = computed(() =>
  frequencyMode.value === 'centerSpan'
    ? spanDraft.value
    : draft.stopHz - draft.startHz,
)
const revisionChanged = computed(
  () => dirty.value && props.status?.revision !== editRevision.value,
)
const locked = computed(() => props.busy || applying.value)

/** 只为草稿预览换算边界；实际命令发送 Center/Span，由后端执行联动。 */
function previewConfiguration(): SweepConfiguration {
  const current = props.status ? configuration(props.status) : { ...draft }
  if (props.mode === 'points') return { ...current, points: draft.points,
    sweepTimeAuto: draft.sweepTimeAuto, sweepTimeSeconds: draft.sweepTimeSeconds }
  if (props.mode === 'bandwidth')
    return { ...current, ifBandwidthHz: draft.ifBandwidthHz }
  if (props.mode === 'power') return { ...current, powerDbm: draft.powerDbm }
  return {
    ...current,
    powerDbm: draft.powerDbm,
    startHz:
      frequencyMode.value === 'centerSpan'
        ? centerDraft.value - spanDraft.value / 2
        : draft.startHz,
    stopHz:
      frequencyMode.value === 'centerSpan'
        ? centerDraft.value + spanDraft.value / 2
        : draft.stopHz,
  }
}
function beginCenterSpan() {
  if (frequencyMode.value === 'centerSpan') return
  centerDraft.value = centerHz.value
  spanDraft.value = spanHz.value
  frequencyMode.value = 'centerSpan'
}
function setCenter(value: number) {
  beginCenterSpan()
  centerDraft.value = value
  update()
}
function setSpan(value: number) {
  beginCenterSpan()
  spanDraft.value = value
  update()
}
function setEdge(edge: 'startHz' | 'stopHz', value: number) {
  const preview = previewConfiguration()
  draft.startHz = preview.startHz
  draft.stopHz = preview.stopHz
  frequencyMode.value = 'edges'
  draft[edge] = value
  update()
}

function discard() {
  if (props.status && !locked.value) load(props.status)
}

function pointsPatch(): SweepPatch {
  const patch: SweepPatch = {}
  if (draft.points !== baseline?.points) patch.points = draft.points
  if (props.status?.capabilities.manualSweepTimeSupported) {
    if (draft.sweepTimeAuto !== baseline?.sweepTimeAuto) patch.sweepTimeAuto = draft.sweepTimeAuto
    if (draft.sweepTimeSeconds !== baseline?.sweepTimeSeconds) {
      patch.sweepTimeSeconds = draft.sweepTimeSeconds
      patch.sweepTimeAuto = draft.sweepTimeAuto
    }
  }
  return patch
}

async function commit() {
  if (!dirty.value || !props.canConfigure || locked.value || !props.status)
    return
  // 每个菜单只发送用户在本页编辑的字段，避免覆盖其它菜单的后端设置。
  const candidate: SweepPatch =
    props.mode === 'points'
      ? pointsPatch()
      : props.mode === 'bandwidth'
        ? { ifBandwidthHz: draft.ifBandwidthHz }
        : props.mode === 'power'
          ? { powerDbm: draft.powerDbm }
        : frequencyMode.value === 'centerSpan'
          ? { centerHz: centerDraft.value, spanHz: spanDraft.value, ...(draft.powerDbm === undefined ? {} : { powerDbm: draft.powerDbm }) }
          : { startHz: draft.startHz, stopHz: draft.stopHz, ...(draft.powerDbm === undefined ? {} : { powerDbm: draft.powerDbm }) }
  if (Object.keys(candidate).length === 0) return
  const expectedRevision = editRevision.value
  const limits = props.status.capabilities
  if (!sweepWithinCapabilities(previewConfiguration(), limits)) {
    validation.value =
      props.mode === 'points' && draft.sweepTimeSeconds !== undefined &&
        (!Number.isFinite(draft.sweepTimeSeconds) || draft.sweepTimeSeconds <= 0 || draft.sweepTimeSeconds > 100000)
        ? 'Sweep Time must be greater than 0 and no more than 100000 s.'
      : props.mode === 'points'
        ? `Number of Points must be an integer between ${limits.minSweepPoints} and ${limits.maxSweepPoints}.`
        : draft.powerDbm !== undefined && (!Number.isFinite(draft.powerDbm) || draft.powerDbm < -120 || draft.powerDbm > 100)
          ? 'Power must be a finite dBm value between -120 and 100.'
        : props.mode === 'bandwidth'
          ? 'IF Bandwidth must be at least 10 Hz.'
          : `Start ≤ Stop · ${frequencyText(limits.minFrequencyHz)}–${frequencyText(limits.maxFrequencyHz)} · Span 0 = CW`
    return
  }
  const submitted = signature(previewConfiguration())
  applying.value = true
  validation.value = ''
  try {
    const result = await props.apply(candidate, expectedRevision, editChannelId.value)
    if (result) {
      if (signature(previewConfiguration()) === submitted) load(result)
      else {
        baseline = configuration(result)
        editRevision.value = result.revision
        dirty.value = signature(previewConfiguration()) !== signature(baseline)
      }
    }
  } finally {
    applying.value = false
  }
}

function applyFromKeyboard(event: KeyboardEvent) {
  if (event.target instanceof HTMLInputElement) {
    event.preventDefault()
    void commit()
  }
}
</script>

<template>
  <section
    class="softtool-section"
    :aria-label="
      mode === 'frequency'
        ? 'Stimulus settings'
        : mode === 'points'
          ? 'Sweep parameters'
          : mode === 'power' ? 'Power settings' : 'IF bandwidth settings'
    "
    @keydown.enter="applyFromKeyboard"
  >
    <template v-if="status">
      <template v-if="mode === 'frequency'">
        <h3 class="softtool-section-title">Stimulus</h3>
        <FrequencyField
          id="stimulus-start"
          label="Start"
          :model-value="previewConfiguration().startHz"
          @update:model-value="setEdge('startHz', $event)"
        />
        <FrequencyField
          id="stimulus-stop"
          label="Stop"
          :model-value="previewConfiguration().stopHz"
          @update:model-value="setEdge('stopHz', $event)"
        />
        <div class="softtool-divider" />
        <FrequencyField
          id="stimulus-center"
          label="Center"
          :model-value="centerHz"
          @update:model-value="setCenter"
        />
        <FrequencyField
          id="stimulus-span"
          label="Span"
          :model-value="spanHz"
          @update:model-value="setSpan"
        />
        <label v-if="status.config.powerDbm !== undefined" class="field-row" for="stimulus-power">
          <span class="softtool-label">Power (dBm)</span>
          <input id="stimulus-power" class="field-input" type="number" v-model.number="draft.powerDbm" :min="status.capabilities.minPowerDbm ?? -120" :max="status.capabilities.maxPowerDbm ?? 100" step="0.1" @input="update" />
        </label>
      </template>
      <template v-else-if="mode === 'power'">
        <h3 class="softtool-section-title">Power · Ch{{ status.activeChannelId ?? 1 }}</h3>
        <label class="field-row" for="channel-power">
          <span class="softtool-label">Channel Base Power (dBm)</span>
          <input id="channel-power" class="field-input" type="number" v-model.number="draft.powerDbm" :min="status.capabilities.minPowerDbm ?? -120" :max="status.capabilities.maxPowerDbm ?? 100" step="0.1" :disabled="status.config.powerDbm === undefined || locked" @input="update" />
        </label>
        <p class="softtool-note">设置当前通道全部扫频点的源功率。各通道独立保存；逐端口功率偏移尚未实现。</p>
        <p v-if="status.config.powerDbm === undefined" class="softtool-note">当前服务未提供功率设置，请更新服务。</p>
      </template>
      <template v-else-if="mode === 'points'">
        <h3 class="softtool-section-title">Sweep Params</h3>
        <label class="field-row" for="sweep-points">
          <span class="softtool-label">Number of Points</span>
          <input
            id="sweep-points"
            v-model.number="draft.points"
            data-testid="points"
            class="field-input"
            type="number"
            :min="status.capabilities.minSweepPoints"
            :max="status.capabilities.maxSweepPoints"
            step="1"
            @input="update"
          />
        </label>
        <div class="softtool-divider" />
        <SweepTimeFields
          v-if="status.config.sweepTimeAuto !== undefined && status.config.sweepTimeSeconds !== undefined"
          :automatic="draft.sweepTimeAuto ?? true" :seconds="draft.sweepTimeSeconds ?? 1"
          :actual-seconds="status.lastSweepTimeSeconds"
          :disabled="!status.capabilities.manualSweepTimeSupported || !canConfigure || locked"
          @update:automatic="draft.sweepTimeAuto = $event; update()"
          @update:seconds="draft.sweepTimeSeconds = $event; update()"
        />
        <p v-if="!status.capabilities.manualSweepTimeSupported" class="softtool-note">当前后端未提供可调软件扫时。</p>
      </template>
      <template v-else>
        <h3 class="softtool-section-title">Bandwidth</h3>
        <label class="field-row" for="channel-bandwidth">
          <span class="softtool-label">IF Bandwidth</span>
          <select
            id="channel-bandwidth"
            v-model.number="draft.ifBandwidthHz"
            class="field-input"
            @change="update"
          >
            <option :value="10">10 Hz</option>
            <option :value="100">100 Hz</option>
            <option :value="1000">1 kHz</option>
            <option :value="10000">10 kHz</option>
            <option :value="100000">100 kHz</option>
          </select>
        </label>
      </template>
      <div class="softtool-apply">
        <button
          class="softtool-button softtool-primary"
          data-testid="apply-config"
          type="button"
          :disabled="!dirty || !canConfigure || locked"
          @click="commit"
        >
          {{ applying ? 'Applying…' : 'Apply' }}
        </button>
        <button
          class="softtool-button"
          type="button"
          :disabled="!dirty || locked"
          @click="discard"
        >
          Reload
        </button>
      </div>
      <p v-if="validation" class="softtool-note" role="status">
        {{ validation }}
      </p>
      <p v-else-if="revisionChanged" class="softtool-note" role="status">
        Device configuration changed. Reload to edit current settings.
      </p>
      <p v-else-if="dirty" class="softtool-note">
        Draft · revision {{ editRevision }}
      </p>
      <p v-else class="softtool-note">
        Applied · revision {{ status.revision }}
      </p>
      <p v-if="!canConfigure && !busy" class="softtool-note">
        Acquire control and wait for the current operation to apply.
      </p>
      <p class="softtool-note">
        {{ status.capabilities.portCount }} ports ·
        {{ frequencyText(status.capabilities.minFrequencyHz) }}–{{
          frequencyText(status.capabilities.maxFrequencyHz)
        }}
      </p>
    </template>
    <p v-else class="softtool-note">Waiting for instrument settings…</p>
  </section>
</template>
