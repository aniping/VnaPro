<script setup lang="ts">
import { computed, ref } from 'vue'
import type {
  InstrumentStatus,
  TraceCreationOptions,
  TraceDestination,
} from '../../api/types'
import {
  sParameterChoices,
  type SParameterChoice,
} from '../../domain/measurements'
import SParameterDialog from './SParameterDialog.vue'

const props = defineProps<{
  status: InstrumentStatus
  destination: TraceDestination
  disabled: boolean
  create: (
    options: TraceCreationOptions,
    expectedRevision: string,
    expectedDisplayRevision: string,
  ) => Promise<InstrumentStatus | null>
}>()
const emit = defineEmits<{ close: [] }>()
// 冻结打开时的双版本和目标图窗，防止等待期间重编号后写入错误图窗。
const revision = props.status.revision
const displayRevision = props.status.display.revision
const destination = { ...props.destination }
const working = ref(false)
const validation = ref('')
const stale = computed(
  () =>
    props.status.revision !== revision ||
    props.status.display.revision !== displayRevision,
)

async function select(parameter: SParameterChoice) {
  if (
    props.disabled ||
    working.value ||
    stale.value ||
    !sParameterChoices(props.status.capabilities.portCount).includes(parameter)
  )
    return
  working.value = true
  validation.value = ''
  try {
    const result = await props.create(
      { ...destination, parameter },
      revision,
      displayRevision,
    )
    if (result) emit('close')
    else validation.value = '新迹线未创建，请检查状态栏中的原因。'
  } finally {
    working.value = false
  }
}
</script>

<template>
  <SParameterDialog
    purpose="new"
    :port-count="status.capabilities.portCount"
    :disabled="disabled || working || stale"
    :busy="working"
    :error="
      stale && !working
        ? '图窗或设备设置已变化，请关闭后重新打开 New Trace。'
        : validation
    "
    @select="select"
    @close="!working && emit('close')"
  />
</template>
