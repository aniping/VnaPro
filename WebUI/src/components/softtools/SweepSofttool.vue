<script setup lang="ts">
import { computed, ref } from 'vue'
import type { InstrumentStatus, SweepControlCommand } from '../../api/types'

const props = defineProps<{
  status: InstrumentStatus | null
  mode: 'sweep-control' | 'trigger'
  canWrite: boolean
  canStop?: boolean
  busy: boolean
  action: (command: SweepControlCommand) => Promise<InstrumentStatus | null>
}>()
const working = ref(false)
const supported = computed(() => Boolean(props.status?.config.selectedSweepMode))
const disabled = computed(
  () => !props.status || !supported.value || !props.canWrite || props.busy || working.value,
)
const continuous = computed(() => props.status?.config.selectedSweepMode === 'CONT')
const single = computed(() => props.status?.config.selectedSweepMode === 'SING')
const manual = computed(() => props.status?.config.triggerSource === 'MAN')
const running = computed(
  () => props.status?.state === 'Running' && props.status.config.sweepMode !== 'HOLD',
)
const armed = computed(() => running.value && manual.value)
const runStatus = computed(() => {
  if (!props.status) return 'Waiting for instrument…'
  if (!supported.value) return 'Sweep controls unavailable'
  if (props.busy || working.value) return 'Waiting for current operation…'
  if (props.status.state === 'Faulted') return 'Stopped · instrument fault'
  if (!running.value) return 'Stopped'
  if (manual.value) return 'Waiting for manual trigger'
  return continuous.value ? 'Sweeping continuously' : 'Acquiring one sweep'
})

async function run(command: SweepControlCommand) {
  if (disabled.value) return
  if (command.action === 'trigger' && !armed.value) return
  if (command.action === 'mode' && command.mode === props.status?.config.selectedSweepMode) return
  if (command.action === 'source' && command.source === props.status?.config.triggerSource) return
  working.value = true
  try {
    await props.action(command)
  } finally {
    working.value = false
  }
}
</script>

<template>
  <section
    class="softtool-section sweep-controls"
    :aria-label="mode === 'trigger' ? 'Trigger In controls' : 'Sweep controls'"
  >
    <h3 class="softtool-section-title">{{ mode === 'trigger' ? 'Trigger In' : 'Sweep Control' }} · Ch{{ status?.activeChannelId ?? 1 }}</h3>
    <p class="sweep-run-status" data-testid="sweep-status" role="status">{{ runStatus }}</p>
    <p v-if="status && !supported" class="softtool-note warning">当前服务未提供独立扫频模式，请更新服务后使用这些控制。</p>
    <p v-else-if="!canWrite && !busy && !working" class="softtool-note">Acquire instrument control to change sweep settings or start a sweep.</p>
    <template v-if="mode === 'sweep-control'">
      <div class="sweep-options" role="group" aria-label="Sweep mode">
        <button
          class="softtool-button sweep-choice"
          :class="{ 'is-active': continuous }"
          data-testid="sweep-continuous"
          type="button"
          :disabled="disabled"
          :aria-pressed="continuous"
          @click="run({ action: 'mode', mode: 'CONT' })"
        >Continuous</button>
        <button
          class="softtool-button sweep-choice"
          :class="{ 'is-active': single }"
          data-testid="sweep-single"
          type="button"
          :disabled="disabled"
          :aria-pressed="single"
          @click="run({ action: 'mode', mode: 'SING' })"
        >Single</button>
      </div>
      <button class="softtool-button sweep-field" data-testid="sweep-count" type="button" disabled aria-describedby="sweep-count-reason">
        <span>Sweeps</span><strong>1</strong>
      </button>
      <p id="sweep-count-reason" class="softtool-note">目前每次 Single 仅支持 1 次扫频。</p>
      <button
        class="softtool-button"
        data-testid="sweep-start"
        type="button"
        :disabled="disabled"
        @click="run({ action: 'start' })"
      >Start Sweep</button>
      <button class="softtool-button" data-testid="sweep-stop" type="button" :disabled="!canStop"
        @click="canStop && action({ action: 'stop' })">Stop Sweep</button>
      <p class="softtool-note">Continuous 持续执行，Manual 下等待触发；Single 使用 Start Sweep 开始新一轮。</p>
      <div class="softtool-divider" />
      <button class="softtool-button" data-testid="sweep-restart-manager" type="button" disabled aria-describedby="sweep-restart-reason">Restart Manager</button>
      <p id="sweep-restart-reason" class="softtool-note">尚未实现可配置的重启范围与多次扫描管理。</p>
      <div class="sweep-options" role="group" aria-label="All Channels">
        <button class="softtool-button" data-testid="sweep-all-continuous" type="button" disabled aria-describedby="sweep-channels-reason">All Channels Continuous</button>
        <button class="softtool-button" data-testid="sweep-all-hold" type="button" disabled aria-describedby="sweep-channels-reason">All Channels on Hold</button>
      </div>
      <p id="sweep-channels-reason" class="softtool-note">尚未实现批量通道控制；可分别选择各通道设置扫描模式。</p>
      <div class="sweep-options">
        <button class="softtool-button sweep-field" data-testid="sweep-controller" type="button" disabled aria-describedby="sweep-controller-reason"><span>Sweep Controller</span><strong>Off</strong></button>
      </div>
      <p id="sweep-controller-reason" class="softtool-note">尚未实现显示扫频阶段的 Sweep Info 窗口。</p>
      <button class="softtool-button sweep-field" data-testid="sweep-pipelining" type="button" disabled aria-describedby="sweep-pipelining-reason"><span>Pipelining</span><strong>Off</strong></button>
      <p id="sweep-pipelining-reason" class="softtool-note">尚未实现流水线采集。</p>
    </template>
    <template v-else>
      <div class="sweep-options" role="group" aria-label="Trigger source">
        <button
          class="softtool-button sweep-choice"
          :class="{ 'is-active': status?.config.triggerSource === 'IMM' }"
          data-testid="sweep-free-run"
          type="button"
          :disabled="disabled"
          :aria-pressed="status?.config.triggerSource === 'IMM'"
          @click="run({ action: 'source', source: 'IMM' })"
        >Free Run</button>
        <button class="softtool-button sweep-choice" data-testid="sweep-external" type="button" disabled aria-pressed="false" aria-describedby="sweep-external-reason">External</button>
        <button class="softtool-button sweep-choice" data-testid="sweep-multiple" type="button" disabled aria-pressed="false" aria-describedby="sweep-external-reason">Multiple</button>
        <button
          class="softtool-button sweep-choice"
          :class="{ 'is-active': manual }"
          data-testid="sweep-manual"
          type="button"
          :disabled="disabled"
          :aria-pressed="manual"
          @click="run({ action: 'source', source: 'MAN' })"
        >Manual</button>
      </div>
      <p id="sweep-external-reason" class="softtool-note">External / Multiple 尚缺外部触发输入与多触发源支持。</p>
      <p class="softtool-note">切换触发源保留当前运行或停止状态。</p>
      <button
        class="softtool-button"
        data-testid="sweep-trigger"
        type="button"
        :disabled="disabled || !armed"
        aria-describedby="sweep-trigger-help"
        @click="run({ action: 'trigger' })"
      >Manual Trigger</button>
      <p id="sweep-trigger-help" class="softtool-note">在 Manual 且仪器等待触发时，Manual Trigger 采一帧。</p>
      <div class="softtool-divider" />
      <div class="sweep-options">
        <button class="softtool-button sweep-field" data-testid="sweep-sequence" type="button" disabled aria-describedby="sweep-sequence-reason"><span>Sequence</span><strong>Sweep</strong></button>
        <button class="softtool-button sweep-field" data-testid="sweep-delay" type="button" disabled aria-describedby="sweep-sequence-reason"><span>Delay</span><strong>0 s</strong></button>
        <button class="softtool-button sweep-field" data-testid="sweep-signal-type" type="button" disabled aria-describedby="sweep-signal-reason"><span>Signal Type</span><strong>—</strong></button>
      </div>
      <p id="sweep-sequence-reason" class="softtool-note">目前固定整次 Sweep、延时 0 s；尚未支持点触发或可调延时。</p>
      <p id="sweep-signal-reason" class="softtool-note">Signal Type 依赖尚未实现的外部触发输入。</p>
      <button class="softtool-button" data-testid="sweep-trigger-manager" type="button" disabled aria-describedby="sweep-trigger-manager-reason">Trigger Manager</button>
      <p id="sweep-trigger-manager-reason" class="softtool-note">尚未实现 Multiple 的分序列触发源、信号条件与延时配置。</p>
    </template>
  </section>
</template>

<style scoped>
.sweep-options { display: grid; gap: 5px; }
.sweep-choice { display: flex; align-items: center; gap: 8px; text-align: left; }
.sweep-choice::before { content: ''; flex: 0 0 14px; width: 14px; height: 14px; border: 1px solid currentColor; border-radius: 50%; }
.sweep-choice.is-active::before { background: #7cd6ff; box-shadow: inset 0 0 0 3px var(--color-button-active); }
.sweep-field { display: flex; align-items: center; justify-content: space-between; gap: 10px; text-align: left; }
.sweep-field strong { flex-shrink: 0; font-weight: 400; }
.sweep-run-status { margin: 0; color: #e4eef3; font-size: 12px; line-height: 1.5; }
</style>
