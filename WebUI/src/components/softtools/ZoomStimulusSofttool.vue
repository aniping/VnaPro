<script setup lang="ts">
import { computed, ref } from 'vue'

const props = defineProps<{
  canZoom: boolean
  canZoomOut: boolean
  busy: boolean
  selecting: boolean
  reason: string
  choose: () => void
  zoomOut: () => Promise<boolean>
}>()
const restoring = ref(false)
const locked = computed(() => props.busy || restoring.value)

function choose() {
  if (!props.canZoom || locked.value) return
  props.choose()
}

async function zoomOut() {
  if (!props.canZoomOut || locked.value) return
  restoring.value = true
  try {
    await props.zoomOut()
  } finally {
    restoring.value = false
  }
}
</script>

<template>
  <section class="softtool-section" aria-label="Zoom Stimulus controls">
    <h3 class="softtool-section-title">Zoom Stimulus</h3>
    <button
      type="button"
      class="softtool-button"
      :class="{ 'is-active': selecting }"
      :aria-pressed="selecting"
      :disabled="!canZoom || locked"
      data-testid="stimulus-zoom-in"
      @click="choose"
    >Zoom In</button>
    <button
      type="button"
      class="softtool-button"
      :disabled="!canZoomOut || locked"
      data-testid="stimulus-zoom-out"
      @click="zoomOut"
    >Zoom Out</button>
    <p v-if="restoring" class="softtool-note" role="status">正在恢复上次缩放范围…</p>
    <p v-else-if="selecting" class="softtool-note" role="status">在图中拖动框选；再次点击 Zoom In 取消选择。</p>
    <p v-if="reason" class="softtool-note warning" role="status">{{ reason }}</p>
    <div class="softtool-divider" />
    <p class="softtool-note">Zoom In 框选真实扫频频段与纵轴范围，扫描点数保持不变。</p>
    <p class="softtool-note">Zoom Out 返回上一次缩放范围。</p>
  </section>
</template>
