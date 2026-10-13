<script setup lang="ts">
import { onMounted, ref } from 'vue'

const props = defineProps<{
  title: string
  message: string
  busy: boolean
  disabled: boolean
  error: string
}>()
const emit = defineEmits<{ confirm: []; cancel: [] }>()
const dialog = ref<HTMLDialogElement | null>(null)

function confirm() {
  if (!props.busy && !props.disabled) emit('confirm')
}
function cancel() {
  // 未提交时取消没有仪器副作用，已受理时等待终态。
  if (!props.busy) emit('cancel')
}
onMounted(() => dialog.value?.showModal())
</script>

<template>
  <dialog
    ref="dialog"
    class="confirmation-dialog"
    aria-labelledby="confirmation-title"
    :aria-busy="busy"
    @cancel.prevent="cancel"
  >
    <header>
      <h2 id="confirmation-title">{{ title }}</h2>
      <button
        type="button"
        :disabled="busy"
        aria-label="Close confirmation"
        @click="cancel"
      >
        ×
      </button>
    </header>
    <p class="confirmation-message">{{ message }}</p>
    <p v-if="error" class="confirmation-error" role="alert">{{ error }}</p>
    <footer>
      <button
        type="button"
        data-testid="confirmation-yes"
        :disabled="busy || disabled"
        @click="confirm"
      >
        Yes
      </button>
      <button
        type="button"
        data-testid="confirmation-no"
        :disabled="busy"
        :autofocus="true"
        @click="cancel"
      >
        No
      </button>
    </footer>
  </dialog>
</template>

<style scoped>
.confirmation-dialog {
  width: 440px;
  max-width: calc(100vw - 24px);
  padding: 0;
  border: 1px solid #708693;
  color: #eef4f7;
  background: #000;
  font-size: 13px;
}
.confirmation-dialog::backdrop {
  background: #0008;
}
.confirmation-dialog header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 9px 12px;
  background: #5d6264;
}
.confirmation-dialog h2 {
  margin: 0;
  font-size: 14px;
  font-weight: 500;
}
.confirmation-dialog header button {
  padding: 0 3px;
  border: 0;
  background: transparent;
  color: inherit;
  font-size: 21px;
}
.confirmation-message {
  margin: 20px;
  line-height: 1.7;
}
.confirmation-error {
  margin: 0 20px 16px;
  color: #ffd580;
}
.confirmation-dialog footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  padding: 0 20px 20px;
}
.confirmation-dialog footer button {
  min-width: 75px;
  padding: 8px;
  border: 1px solid #708693;
  color: inherit;
  background: #455f6f;
}
.confirmation-dialog button:disabled {
  color: #81959f;
  cursor: default;
}
.confirmation-dialog button:focus-visible {
  outline: 2px solid #d8edfa;
  outline-offset: -3px;
}
</style>
