<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import {
  sParameterChoices,
  type SParameterChoice,
} from '../../domain/measurements'

const props = withDefaults(
  defineProps<{
    purpose?: 'measurement' | 'new'
    portCount: number
    parameter?: string
    disabled: boolean
    busy?: boolean
    error: string
  }>(),
  { purpose: 'measurement' },
)
const emit = defineEmits<{
  select: [parameter: SParameterChoice]
  close: []
}>()
const dialog = ref<HTMLDialogElement | null>(null)
const parameters = computed(() => sParameterChoices(props.portCount))
const title = computed(() =>
  props.purpose === 'new' ? 'New Trace' : 'S-Parameters',
)

function select(parameter: SParameterChoice) {
  if (!props.disabled && !props.busy && parameters.value.includes(parameter))
    emit('select', parameter)
}

function close() {
  // 已接受的设备操作不能被关闭弹框伪装成取消。
  if (!props.busy) emit('close')
}

onMounted(() => dialog.value?.showModal())
</script>

<template>
  <dialog
    ref="dialog"
    class="s-parameter-dialog"
    aria-labelledby="s-parameter-dialog-title"
    :aria-busy="busy === true"
    @cancel.prevent="close"
  >
    <header>
      <h2 id="s-parameter-dialog-title">{{ title }}</h2>
      <button
        type="button"
        :disabled="busy"
        aria-label="Close S parameters"
        @click="close"
      >
        ×
      </button>
    </header>
    <p class="s-parameter-description">
      {{
        purpose === 'new'
          ? 'Select S-Parameter for the new trace'
          : 'Select S-Parameter for the active trace'
      }}
    </p>
    <div
      class="s-parameter-matrix"
      role="group"
      aria-label="S-Parameter matrix"
      :style="{
        gridTemplateColumns: `repeat(${Math.max(1, portCount)}, minmax(0, 1fr))`,
      }"
    >
      <button
        v-for="(value, index) in parameters"
        :key="value"
        type="button"
        :disabled="disabled || busy"
        :aria-pressed="parameter === value"
        :autofocus="parameter === value || (!parameter && index === 0)"
        :data-parameter="value"
        @click="select(value)"
      >
        {{ value }}
      </button>
    </div>
    <p v-if="error" class="s-parameter-error" role="alert">{{ error }}</p>
  </dialog>
</template>

<style scoped>
.s-parameter-dialog {
  width: 409px;
  max-width: calc(100vw - 24px);
  padding: 0;
  border: 1px solid #708693;
  color: #eef4f7;
  background: #000;
  font-size: 13px;
}
.s-parameter-dialog::backdrop {
  background: #0008;
}
.s-parameter-dialog header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 9px 12px;
  background: #5d6264;
}
.s-parameter-dialog h2 {
  margin: 0;
  font-size: 14px;
  font-weight: 500;
}
.s-parameter-dialog header button {
  padding: 0 3px;
  border: 0;
  color: inherit;
  background: transparent;
  font-size: 21px;
}
.s-parameter-description {
  margin: 16px 16px 10px;
}
.s-parameter-matrix {
  display: grid;
  gap: 5px;
  margin: 0 16px 16px;
}
.s-parameter-matrix button {
  min-height: 42px;
  padding: 8px;
  border: 1px solid #708693;
  color: inherit;
  background: #455f6f;
  font-size: 13px;
}
.s-parameter-matrix button[aria-pressed='true'] {
  border-color: #49bded;
  background: #0d75bf;
}
.s-parameter-matrix button:hover:not(:disabled) {
  background: #547589;
}
.s-parameter-dialog button:disabled {
  color: #81959f;
  cursor: default;
}
.s-parameter-dialog button:focus-visible {
  outline: 2px solid #d8edfa;
  outline-offset: -3px;
}
.s-parameter-error {
  margin: 0 16px 16px;
  color: #ffd580;
}
</style>
