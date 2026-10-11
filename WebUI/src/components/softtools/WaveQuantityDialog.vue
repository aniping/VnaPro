<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import type { Measurement, MeasurementCandidate } from '../../api/types'

const props = defineProps<{
  measurement: Measurement
  portCount: number
  disabled: boolean
  error: string
}>()
const emit = defineEmits<{
  apply: [candidate: MeasurementCandidate]
  close: []
}>()
const dialog = ref<HTMLDialogElement | null>(null)
const draft = reactive<MeasurementCandidate>({
  name: props.measurement.name,
  kind: 'receiver',
  receiverKind: props.measurement.receiverKind,
  receiverPort: props.measurement.receiverPort,
  sourcePort: props.measurement.sourcePort,
})
onMounted(() => dialog.value?.showModal())
</script>

<template>
  <dialog
    ref="dialog"
    class="wave-dialog"
    aria-labelledby="wave-dialog-title"
    @cancel.prevent="emit('close')"
  >
    <header>
      <h2 id="wave-dialog-title">More Wave Quantities</h2>
      <button
        type="button"
        aria-label="Close wave quantities"
        @click="emit('close')"
      >
        ×
      </button>
    </header>
    <form @submit.prevent="emit('apply', { ...draft })">
      <fieldset :disabled="disabled">
        <legend>Wave</legend>
        <label v-for="kind in ['a', 'b'] as const" :key="kind">
          <input
            v-model="draft.receiverKind"
            type="radio"
            name="wave-kind"
            :value="kind"
          />
          {{ kind }}
        </label>
      </fieldset>
      <label class="wave-field" for="wave-receiver-port">
        Receiver Port
        <select
          id="wave-receiver-port"
          v-model.number="draft.receiverPort"
          :disabled="disabled"
        >
          <option v-for="port in portCount" :key="port" :value="port">
            {{ port }}
          </option>
        </select>
      </label>
      <label class="wave-field" for="wave-source-port">
        Source Port
        <select
          id="wave-source-port"
          v-model.number="draft.sourcePort"
          :disabled="disabled"
        >
          <option v-for="port in portCount" :key="port" :value="port">
            {{ port }}
          </option>
        </select>
      </label>
      <p>
        {{ draft.receiverKind }}{{ draft.receiverPort }} · Src Port
        {{ draft.sourcePort }}
      </p>
      <p v-if="error" role="alert">{{ error }}</p>
      <footer>
        <button type="button" :disabled="disabled" @click="emit('close')">
          Cancel
        </button>
        <button type="submit" :disabled="disabled">OK</button>
      </footer>
    </form>
  </dialog>
</template>

<style scoped>
.wave-dialog {
  width: 330px;
  padding: 0;
  border: 1px solid #708693;
  color: white;
  background: #1d282f;
  font-size: 13px;
}
.wave-dialog::backdrop {
  background: #0008;
}
.wave-dialog header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 9px 12px;
  background: #33434d;
}
.wave-dialog h2 {
  margin: 0;
  font-size: 14px;
  font-weight: 500;
}
.wave-dialog header button {
  border: 0;
  color: white;
  background: transparent;
  font-size: 21px;
}
.wave-dialog form {
  display: grid;
  gap: 20px;
  padding: 20px;
}
.wave-dialog fieldset {
  display: flex;
  gap: 25px;
  padding: 10px;
  border: 1px solid #647985;
}
.wave-field {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.wave-field select {
  width: 90px;
  padding: 7px;
  color: white;
  background: #455f6f;
  border: 1px solid #708693;
}
.wave-dialog p {
  margin: 0;
  color: #bfccd4;
}
.wave-dialog footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
.wave-dialog footer button {
  min-width: 75px;
  padding: 8px;
  color: white;
  background: #455f6f;
  border: 1px solid #708693;
}
.wave-dialog button:disabled {
  color: #81959f;
}
</style>
