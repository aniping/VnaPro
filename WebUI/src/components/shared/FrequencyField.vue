<script setup lang="ts">
import { computed, ref } from 'vue'
const props = defineProps<{ modelValue: number; label: string; id?: string; disabled?: boolean }>()
const emit = defineEmits<{ 'update:modelValue': [value: number]; blur: [] }>()
const unit = ref(1e6)
const units = [{ value: 1, label: 'Hz' }, { value: 1e3, label: 'kHz' }, { value: 1e6, label: 'MHz' }, { value: 1e9, label: 'GHz' }]
const value = computed(() => Number.isFinite(props.modelValue) ? Number((props.modelValue / unit.value).toPrecision(12)) : '')
function update(event: Event) {
  const input = event.target as HTMLInputElement
  emit('update:modelValue', input.value === '' || !Number.isFinite(input.valueAsNumber) ? NaN : input.valueAsNumber * unit.value)
}
</script>

<template>
  <div class="field-row frequency-field">
    <label class="softtool-label" :for="id">{{ label }}</label>
    <div class="frequency-input">
      <input :id="id" :data-testid="id" class="field-input" type="number" step="any" :value="value" :aria-label="label" :disabled="disabled" @input="update" @blur="emit('blur')">
      <select v-model="unit" :aria-label="`${label} unit`" :disabled="disabled"><option v-for="option in units" :key="option.value" :value="option.value">{{ option.label }}</option></select>
    </div>
  </div>
</template>
