<script setup lang="ts">
const props = defineProps<{ activeKey: string | null; hasTrace: boolean }>();
const emit = defineEmits<{ select: [id: string] }>();

interface Hardkey {
  id: string;
  lines: string[];
  label: string;
  requiresTrace?: boolean;
  unsupported?: string;
  accent?: 'help' | 'preset';
}

const groups: { title: string; keys: Hardkey[] }[] = [
  { title: 'Trace', keys: [
    { id: 'meas', lines: ['Meas'], label: 'Meas' },
    { id: 'format', lines: ['Format'], label: 'Format', requiresTrace: true },
    { id: 'scale', lines: ['Scale'], label: 'Scale', requiresTrace: true },
    { id: 'trace-config', lines: ['Trace', 'Config'], label: 'Trace Config' },
    { id: 'line', lines: ['Line'], label: 'Line', unsupported: '尚未实现限值线' },
    { id: 'marker', lines: ['Marker'], label: 'Marker', requiresTrace: true },
  ] },
  { title: 'Stimulus', keys: [
    { id: 'start', lines: ['Start'], label: 'Start' },
    { id: 'stop', lines: ['Stop'], label: 'Stop' },
    { id: 'center', lines: ['Center'], label: 'Center' },
    { id: 'span', lines: ['Span'], label: 'Span' },
  ] },
  { title: 'Channel', keys: [
    { id: 'bandwidth', lines: ['Power', 'Bw Avg'], label: 'Power / Bw / Avg' },
    { id: 'sweep', lines: ['Sweep'], label: 'Sweep' },
    { id: 'cal', lines: ['Cal'], label: 'Cal', unsupported: '尚未实现校准' },
    { id: 'channel-config', lines: ['Channel', 'Config'], label: 'Channel Config', unsupported: '当前仅支持一个通道' },
    { id: 'trigger', lines: ['Trigger'], label: 'Trigger' },
    { id: 'offset', lines: ['Offset', 'Embed'], label: 'Offset / Embed', unsupported: '尚未实现去嵌与偏移' },
  ] },
  { title: 'System', keys: [
    { id: 'file', lines: ['File', 'Print'], label: 'File / Print' },
    { id: 'setup', lines: ['Setup'], label: 'Setup' },
    { id: 'tools', lines: ['Tools'], label: 'Tools' },
    { id: 'display', lines: ['Display'], label: 'Display' },
    { id: 'help', lines: ['?'], label: 'Help', accent: 'help' },
    { id: 'preset', lines: ['Preset'], label: 'Preset', unsupported: '尚未实现仪器预设恢复', accent: 'preset' },
  ] },
];

function disabled(key: Hardkey): boolean {
  return Boolean(key.unsupported || (key.requiresTrace && !props.hasTrace));
}

function select(key: Hardkey) {
  if (!disabled(key)) emit('select', key.id);
}
</script>

<template>
  <nav class="hardkey-panel" aria-label="Instrument function keys">
    <section v-for="group in groups" :key="group.title" class="hardkey-group">
      <h2>{{ group.title }}</h2>
      <div class="hardkey-grid">
        <button v-for="key in group.keys" :key="key.id" type="button" class="hardkey" :class="[key.accent, { active: activeKey === key.id }]" :disabled="disabled(key)" :title="key.unsupported ? `${key.label} — ${key.unsupported}` : key.requiresTrace && !hasTrace ? `${key.label} — 请先选择测量` : key.label" :aria-label="key.label" :aria-pressed="!disabled(key) && activeKey === key.id" :data-hardkey="key.id" @click="select(key)">
          <span v-if="key.accent === 'help'" class="hardkey-help-glyph" aria-hidden="true">?</span>
          <template v-else><span v-for="line in key.lines" :key="line">{{ line }}</span></template>
        </button>
      </div>
    </section>
    <div class="hardkey-footnote">VnaPro<small>SIMULATOR</small></div>
  </nav>
</template>
