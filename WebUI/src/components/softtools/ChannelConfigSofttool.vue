<script setup lang="ts">
import { computed, ref } from 'vue'
import type { ChannelCommand, InstrumentStatus } from '../../api/types'
import { MAX_CHANNELS } from '../../domain/channels'
import { MAX_DIAGRAMS, MAX_TRACES } from '../../domain/presentation'
import { frequencyText } from '../../plot/formatting'

const props = defineProps<{
  status: InstrumentStatus | null
  canWrite: boolean
  busy: boolean
  perform: (command: ChannelCommand) => Promise<InstrumentStatus | null>
}>()
const working = ref(false)
const channels = computed(() => props.status?.channels ?? [])
const activeId = computed(() => props.status?.activeChannelId ?? 1)
const locked = computed(() => !props.canWrite || props.busy || working.value || !channels.value.length)
const canCreate = computed(() => !locked.value && channels.value.length < MAX_CHANNELS && (props.status?.measurements.length ?? MAX_TRACES) < MAX_TRACES)
const canAddDiagram = computed(() => (props.status?.display.diagrams.length ?? MAX_DIAGRAMS) < MAX_DIAGRAMS)
const sourceTraceCount = computed(() => props.status?.measurements.filter((trace) => (trace.channelId ?? 1) === activeId.value).length ?? 0)
const canCopy = computed(() => canCreate.value && sourceTraceCount.value > 0 && (props.status?.measurements.length ?? 0) + sourceTraceCount.value <= MAX_TRACES)

async function act(command: ChannelCommand) {
  if (locked.value || (command.action === 'delete' && channels.value.length <= 1) ||
    ((command.action === 'new' || command.action === 'copy') &&
      (!canCreate.value || (command.newDiagram && !canAddDiagram.value) || (command.action === 'copy' && !canCopy.value)))) return
  working.value = true
  try { await props.perform(command) } finally { working.value = false }
}
function select(event: Event) {
  const select = event.target as HTMLSelectElement
  const channelId = Number(select.value)
  select.value = String(activeId.value)
  if (channelId !== activeId.value && channels.value.some((channel) => channel.channelId === channelId))
    void act({ action: 'select', channelId })
}
</script>

<template>
  <section class="softtool-section" aria-label="Channel configuration">
    <h3 class="softtool-section-title">Channels</h3>
    <label class="field-row" for="active-channel">
      <span>Active Channel</span>
      <select id="active-channel" class="field-input" :value="activeId" :disabled="locked || channels.length <= 1" @change="select">
        <option v-for="channel in channels" :key="channel.channelId" :value="channel.channelId">{{ channel.name }}</option>
      </select>
    </label>
    <div class="channel-summary" v-for="channel in channels" :key="channel.channelId" :class="{ active: channel.channelId === activeId }">
      <span>{{ channel.name }} · {{ channel.config.selectedSweepMode === 'SING' ? 'Single' : 'Continuous' }} · {{ channel.config.triggerSource === 'MAN' ? 'Manual' : 'Free Run' }}</span>
      <span>{{ frequencyText(channel.config.startHz) }} – {{ frequencyText(channel.config.stopHz) }}</span>
    </div>
    <p v-if="!channels.length" class="softtool-note">当前服务未提供多通道接口，请更新服务。</p>
    <button type="button" class="softtool-button" data-testid="channel-new" :disabled="!canCreate" @click="act({ action: 'new', newDiagram: false })">New Channel</button>
    <button type="button" class="softtool-button" data-testid="channel-copy" :disabled="!canCopy" @click="act({ action: 'copy', channelId: activeId, newDiagram: false })">Copy Channel</button>
    <button type="button" class="softtool-button" data-testid="channel-new-diagram" :disabled="!canCreate || !canAddDiagram" @click="act({ action: 'new', newDiagram: true })">New Channel + Diagram</button>
    <button type="button" class="softtool-button" data-testid="channel-copy-diagram" :disabled="!canCopy || !canAddDiagram" @click="act({ action: 'copy', channelId: activeId, newDiagram: true })">Copy Channel + Diagram</button>
    <p class="softtool-note">New 使用默认扫频设置。Copy 复制当前通道的设置、校准和全部迹线；副本独立采集。</p>
    <p v-if="sourceTraceCount === 0" class="softtool-note">当前通道没有迹线，可用工具栏 Add Trace 创建；空通道暂不支持复制。</p>
    <p class="softtool-note">保留原频段并局部扫频：选择 Copy Channel + Diagram，再对新通道使用 Stimulus → Zoom Stimulus。</p>
    <button type="button" class="softtool-button" data-testid="channel-delete" :disabled="locked || channels.length <= 1" @click="act({ action: 'delete', channelId: activeId })">Delete Channel</button>
    <p class="softtool-note">删除通道同时删除所属迹线；至少保留一个通道。本项目最多支持 16 通道、128 条迹线。</p>
  </section>
</template>

<style scoped>
.channel-summary { display: flex; flex-direction: column; gap: 2px; padding: 4px 5px; border-left: 2px solid transparent; color: #b4c5cf; font-size: 11px; }
.channel-summary.active { border-color: #8cd7ff; color: #fff; background: #ffffff08; }
</style>
