<script setup lang="ts">
import { computed, onScopeDispose, ref } from 'vue'
import type { InstrumentStatus } from '../../api/types'
import ConfirmationDialog from './ConfirmationDialog.vue'

const props = defineProps<{
  status: InstrumentStatus | null
  activeTraceName: string
  disabled: boolean
  create: (
    expectedRevision: string,
    expectedDisplayRevision: string,
  ) => Promise<InstrumentStatus | null>
}>()
const emit = defineEmits<{ close: [] }>()
// 确认只保存本次操作意图；版本、能力及仪器目录始终来自后端。
const opening = props.status
  ? {
      revision: props.status.revision,
      displayRevision: props.status.display.revision,
      portCount: props.status.capabilities.portCount,
      name: props.activeTraceName,
      channelId: props.status.activeChannelId ?? 1,
      measurements: props.status.measurements.filter((trace) => (trace.channelId ?? 1) === (props.status?.activeChannelId ?? 1)).length,
    }
  : null
const working = ref(false)
const closed = ref(false)
const error = ref('')
// 取消或销毁立即撤销意图；延迟到达的旧确认事件不能重新执行。
onScopeDispose(() => {
  closed.value = true
})
const current = computed(() =>
  Boolean(
    !closed.value &&
    opening &&
    props.status &&
    opening.revision === props.status.revision &&
    opening.displayRevision === props.status.display.revision &&
    opening.portCount === props.status.capabilities.portCount &&
    opening.name === props.activeTraceName &&
    opening.channelId === (props.status?.activeChannelId ?? 1),
  ),
)
const message = computed(() =>
  opening
    ? `将替换当前通道 Ch${opening.channelId} 的 ${opening.measurements} 条旧测量定义，并创建 ${opening.portCount ** 2} 条 S 参数迹线及图窗，按 ${opening.portCount}×${opening.portCount} 排列。保留其他通道，扫描设置保持不变。是否继续？`
    : '尚未读取仪器状态，请关闭后重试。',
)
const visibleError = computed(
  () =>
    error.value ||
    (!current.value && !working.value
      ? '仪器上下文已变化，请关闭后重新确认。'
      : ''),
)

async function confirm() {
  if (working.value || props.disabled || !current.value || !opening) return
  working.value = true
  error.value = ''
  try {
    const result = await props.create(opening.revision, opening.displayRevision)
    if (closed.value) return
    if (result) {
      closed.value = true
      emit('close')
    } else error.value = 'S 参数整组替换未完成，请查看状态栏中的原因。'
  } catch (failure) {
    if (closed.value) return
    error.value =
      failure instanceof Error
        ? failure.message
        : 'S 参数整组替换失败，请重试。'
  } finally {
    working.value = false
  }
}
function cancel() {
  if (working.value || closed.value) return
  closed.value = true
  emit('close')
}
</script>

<template>
  <ConfirmationDialog
    title="Confirm All S-Params"
    :message="message"
    :busy="working"
    :disabled="disabled || !current"
    :error="visibleError"
    @confirm="confirm"
    @cancel="cancel"
  />
</template>
