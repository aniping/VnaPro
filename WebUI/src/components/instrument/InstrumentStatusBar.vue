<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';

const props = defineProps<{
  connected: boolean;
  coreState: string;
  mode: string;
  activeChannelId?: number;
  controller: 'none' | 'scpi' | 'web';
  hasControl: boolean;
  claimBusy: boolean;
  frameSeq: number | string | null;
  runId: number | string | null;
  revision: number | string | null;
  notice: string;
  noticeKind: string;
  error?: string;
  calibrationLabel?: string;
  calibrationState?: string;
  calibrationTone?: 'neutral' | 'success' | 'warning';
}>();

const emit = defineEmits<{
  selectTool: [id: string];
  acquireControl: [];
  releaseControl: [];
}>();

const menus = [
  ['File', 'file'], ['Trace', 'trace-config'], ['Channel', 'channel-config'],
  ['Display', 'display'], ['Tools', 'tools'], ['System', 'setup'], ['Help', 'help'],
] as const;
const now = ref(new Date());
let clock: ReturnType<typeof setInterval> | undefined;
onMounted(() => { clock = setInterval(() => { now.value = new Date(); }, 1000); });
onBeforeUnmount(() => { if (clock) clearInterval(clock); });
const time = computed(() => now.value.toLocaleTimeString('en-GB', { hour12: false }));
const summary = computed(() => props.error || props.notice || (props.connected ? '' : '连接中…'));
const status = computed(() => !props.connected ? 'Offline' : props.coreState === 'Faulted' ? 'Fault' : props.mode || props.coreState);
const ownerLabel = computed(() => props.hasControl ? 'Local' : props.controller === 'scpi' ? 'SCPI' : props.controller === 'web' ? 'Remote Web' : 'Observer');
</script>

<template>
  <footer class="instrument-status-bar" aria-label="Instrument status">
    <nav class="instrument-menu" aria-label="Application menu">
      <button v-for="[label, id] in menus" :key="id" type="button" :title="label" @click="emit('selectTool', id)">{{ label }}</button>
    </nav>
    <div class="status-message" :class="{ error: Boolean(error) || noticeKind === 'error' }" role="status" aria-live="polite" :title="summary">{{ summary }}</div>
    <div class="status-calibration" :class="connected ? calibrationTone : 'neutral'" data-testid="persistent-calibration-state" role="status" aria-live="polite" :title="connected ? calibrationLabel : '连接中断，无法确认当前校准状态'">{{ connected ? calibrationState || calibrationLabel || 'Cal Off · 未校准' : 'Cal · 状态未确认' }}</div>
    <div class="status-metadata" :title="`Frame ${frameSeq ?? '—'} · Run ${runId ?? '—'} · Configuration revision ${revision ?? '—'}`"><span>F {{ frameSeq ?? '—' }}</span><span>R {{ revision ?? '—' }}</span></div>
    <div class="status-channel"><i :class="{ online: connected, fault: coreState === 'Faulted' }" aria-hidden="true" />Ch{{ activeChannelId ?? 1 }}: {{ status }}</div>
    <button type="button" class="status-control" :class="{ local: hasControl }" :disabled="!connected || claimBusy || (!hasControl && controller !== 'none')" :aria-busy="claimBusy" :title="hasControl ? 'Release control · 释放控制权' : controller === 'none' ? 'Acquire control · 获取控制权' : '控制权由其他客户端持有'" data-testid="control-toggle" @click="hasControl ? emit('releaseControl') : emit('acquireControl')">{{ claimBusy ? 'Waiting…' : ownerLabel }}</button>
    <time class="status-clock" :datetime="now.toISOString()">{{ time }}</time>
  </footer>
</template>
