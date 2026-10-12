<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import type { LayoutMode } from '../../domain/presentation'

const props = defineProps<{
  number: number
  active: boolean
  layout: LayoutMode
  hasTrace: boolean
  canEditDisplay: boolean
}>()
const emit = defineEmits<{
  activate: []
  layout: [mode: LayoutMode]
  addMarker: []
}>()
const root = ref<HTMLElement>()
const trigger = ref<HTMLButtonElement>()
const open = ref(false)
const choices: { mode: LayoutMode; label: string }[] = [
  { mode: 'grid', label: 'All Diagrams' },
  { mode: 'maximized', label: 'Maximized' },
  { mode: 'single', label: 'Overlay All' },
]

function close(restoreFocus = false) {
  open.value = false
  if (restoreFocus) trigger.value?.focus()
}

function items(): HTMLButtonElement[] {
  return Array.from(
    root.value?.querySelectorAll<HTMLButtonElement>(
      '[role^="menuitem"]:not(:disabled)',
    ) ?? [],
  )
}

async function toggle() {
  if (open.value) {
    close(true)
    return
  }
  emit('activate')
  open.value = true
  await nextTick()
  const checked = root.value?.querySelector<HTMLButtonElement>(
    '[aria-checked="true"]',
  )
  ;(checked ?? items()[0])?.focus()
}

async function choose(mode: LayoutMode) {
  const targetNumber = mode === 'single' ? 1 : props.number
  close()
  emit('layout', mode)
  // 布局变化会重新挂载带键值的绘图区；应将焦点恢复到新触发按钮。
  await nextTick()
  const nextTrigger =
    document.querySelector<HTMLButtonElement>(
      `[data-testid="diagram-menu-${targetNumber}"]`,
    ) ?? document.querySelector<HTMLButtonElement>('.diagram-menu-trigger')
  nextTrigger?.focus()
}

function addMarker() {
  if (!props.hasTrace) return
  emit('addMarker')
  close(true)
}

function navigate(event: KeyboardEvent) {
  if (event.key === 'Escape') {
    event.preventDefault()
    event.stopPropagation()
    close(true)
    return
  }
  if (event.key === 'Tab') {
    close()
    return
  }
  if (!['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return
  event.preventDefault()
  event.stopPropagation()
  const buttons = items()
  const current = buttons.findIndex(
    (button) => button === document.activeElement,
  )
  const index =
    event.key === 'Home'
      ? 0
      : event.key === 'End'
        ? buttons.length - 1
        : (current + (event.key === 'ArrowUp' ? -1 : 1) + buttons.length) %
          buttons.length
  buttons[index]?.focus()
}

function outside(event: PointerEvent) {
  if (event.target instanceof Node && !root.value?.contains(event.target))
    close()
}

onMounted(() => document.addEventListener('pointerdown', outside))
onBeforeUnmount(() => document.removeEventListener('pointerdown', outside))
</script>

<template>
  <div
    ref="root"
    class="diagram-menu"
    :class="{ 'is-active': active, 'is-open': open }"
  >
    <button
      ref="trigger"
      class="diagram-menu-trigger"
      type="button"
      :aria-label="`Diagram ${number} menu`"
      aria-haspopup="menu"
      :aria-expanded="open"
      :data-testid="`diagram-menu-${number}`"
      @click.stop="toggle"
      @keydown.down.prevent.stop="open ? navigate($event) : toggle()"
      @keydown.esc.prevent.stop="close(true)"
    >
      <span class="diagram-menu-arrow" aria-hidden="true">▼</span
      ><span>{{ number }}</span>
    </button>
    <div
      v-if="open"
      class="diagram-menu-popup"
      role="menu"
      :aria-label="`Diagram ${number} actions`"
      @keydown="navigate"
    >
      <button
        v-for="choice in choices"
        :key="choice.mode"
        class="diagram-menu-item"
        type="button"
        role="menuitemradio"
        :disabled="!canEditDisplay"
        :aria-checked="layout === choice.mode"
        :data-layout="choice.mode"
        @click.stop="choose(choice.mode)"
      >
        <span class="diagram-menu-check" aria-hidden="true">{{
          layout === choice.mode ? '✓' : ''
        }}</span
        ><span>{{ choice.label }}</span>
      </button>
      <div class="diagram-menu-separator" role="separator" />
      <button
        class="diagram-menu-item"
        type="button"
        role="menuitem"
        :disabled="!hasTrace || !canEditDisplay"
        @click.stop="addMarker"
      >
        <span class="diagram-menu-check" aria-hidden="true">+</span
        ><span>Add Marker</span>
      </button>
    </div>
  </div>
</template>

<style scoped>
.diagram-menu {
  position: relative;
  flex: 0 0 auto;
  font:
    12px Arial,
    sans-serif;
}
.diagram-menu-trigger {
  display: inline-flex;
  height: 22px;
  min-width: 34px;
  align-items: center;
  justify-content: center;
  gap: 5px;
  padding: 0 5px;
  color: #d4e0e9;
  background: #33424b;
  border: 1px solid #596c77;
  border-radius: 0;
  cursor: pointer;
}
.diagram-menu-arrow {
  color: #c0d0dd;
  font-size: 8px;
}
.diagram-menu-trigger:hover,
.is-open .diagram-menu-trigger {
  background: #4a606d;
}
.is-active .diagram-menu-trigger {
  color: #eff7fc;
  border-color: #6f8a9c;
}
.diagram-menu-trigger:focus-visible,
.diagram-menu-item:focus-visible {
  outline: 1px solid #adc9e0;
  outline-offset: -2px;
}
.diagram-menu-popup {
  position: absolute;
  z-index: 60;
  top: 23px;
  right: 0;
  width: 148px;
  padding: 3px 0;
  color: #e8f0f7;
  background: #33424b;
  border: 1px solid #667d87;
  box-shadow: 1px 3px 7px #0008;
}
.diagram-menu-item {
  display: flex;
  width: 100%;
  height: 26px;
  align-items: center;
  gap: 5px;
  padding: 0 8px 0 3px;
  font: inherit;
  color: inherit;
  background: transparent;
  border: 0;
  text-align: left;
  cursor: pointer;
  white-space: nowrap;
}
.diagram-menu-item:hover,
.diagram-menu-item:focus-visible {
  background: #4a606d;
}
.diagram-menu-item:disabled {
  color: #7d93a6;
  cursor: default;
  background: transparent;
}
.diagram-menu-check {
  width: 15px;
  flex: 0 0 15px;
  text-align: center;
}
.diagram-menu-separator {
  height: 1px;
  margin: 3px 5px;
  background: #60768a;
}
</style>
