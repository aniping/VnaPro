<script setup lang="ts">
import { computed, ref } from 'vue'

const props = defineProps<{
  automatic: boolean
  seconds: number
  actualSeconds?: number
  disabled: boolean
}>()
const emit = defineEmits<{
  'update:automatic': [value: boolean]
  'update:seconds': [value: number]
}>()
const unit = ref<'s' | 'ms'>('s')
const factor = computed(() => unit.value === 'ms' ? 0.001 : 1)
const displayValue = computed({
  get: () => props.seconds / factor.value,
  set: (value: number) => {
    if (!props.disabled && !props.automatic) emit('update:seconds', Number(value) * factor.value)
  },
})
const actualText = computed(() => props.actualSeconds && props.actualSeconds > 0
  ? `${Number(props.actualSeconds.toPrecision(4))} s` : '—')
function setAutomatic(event: Event) {
  if (!props.disabled) emit('update:automatic', (event.target as HTMLInputElement).checked)
}
</script>

<template>
  <div class="sweep-time-fields">
    <div class="sweep-time-label">
      <label class="softtool-label" for="sweep-time-value">Sweep Time</label>
      <label class="sweep-time-auto"><input
        id="sweep-time-auto" type="checkbox" :checked="automatic" :disabled="disabled"
        @change="setAutomatic"
      />Auto</label>
    </div>
    <input v-if="automatic" id="sweep-time-value" class="field-input" value="Minimum" readonly aria-label="Automatic Sweep Time" />
    <div v-else class="frequency-input">
      <input id="sweep-time-value" v-model.number="displayValue" class="field-input" type="number"
        :min="Number.MIN_VALUE" :max="100000 / factor" step="any" :disabled="disabled" aria-label="Sweep Time" />
      <select v-model="unit" class="frequency-unit" :disabled="disabled" aria-label="Sweep Time unit">
        <option value="s">s</option><option value="ms">ms</option>
      </select>
    </div>
    <p class="softtool-note">Last Sweep: {{ actualText }}</p>
    <p class="softtool-note">Software pacing · 最小回读扫时，不改变板卡内部逐点测量时间。Auto 使用最快节拍。</p>
  </div>
</template>

<style scoped>
.sweep-time-fields { display: grid; gap: 6px; }
.sweep-time-label { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.sweep-time-auto { display: flex; align-items: center; gap: 5px; font-size: 12px; }
.sweep-time-auto input { margin: 0; accent-color: var(--color-button-active); }
.sweep-time-fields .softtool-note { margin: 0; }
</style>
