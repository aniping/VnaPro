<script setup lang="ts">
import type { TraceDropPosition } from './traceDrag'

defineProps<{
  position: TraceDropPosition | null
  allowSplit: boolean
  moving: boolean
}>()

const zones: { position: TraceDropPosition; points: string; label: string }[] =
  [
    {
      position: 'above',
      points: '0,0 300,0 200,100 100,100',
      label: 'Split horizontally,\nadd trace above',
    },
    {
      position: 'below',
      points: '0,300 300,300 200,200 100,200',
      label: 'Split horizontally,\nadd trace below',
    },
    {
      position: 'left',
      points: '0,0 100,100 100,200 0,300',
      label: 'Split vertically,\nadd trace to the left',
    },
    {
      position: 'right',
      points: '300,0 200,100 200,200 300,300',
      label: 'Split vertically,\nadd trace to the right',
    },
    {
      position: 'center',
      points: '100,100 200,100 200,200 100,200',
      label: 'Add trace in\nexisting diagram',
    },
  ]
</script>

<template>
  <div
    class="trace-drop-zones"
    aria-hidden="true"
    data-testid="trace-drop-zones"
  >
    <svg viewBox="0 0 300 300" preserveAspectRatio="none">
      <template v-for="zone in zones" :key="zone.position">
        <polygon
          v-if="allowSplit || zone.position === 'center'"
          :points="zone.points"
          :class="{ selected: position === zone.position }"
          vector-effect="non-scaling-stroke"
        />
      </template>
    </svg>
    <template v-for="zone in zones" :key="zone.position">
      <span
        v-if="allowSplit || zone.position === 'center'"
        :class="['drop-label', zone.position]"
        >{{
          moving
            ? zone.label
                .replace('add trace', 'move trace')
                .replace('Add trace', 'Move trace')
            : zone.label
        }}</span
      >
    </template>
  </div>
</template>

<style scoped>
.trace-drop-zones {
  position: absolute;
  inset: 0;
  z-index: 12;
  pointer-events: none;
  container-type: inline-size;
}
svg {
  width: 100%;
  height: 100%;
}
polygon {
  fill: #0d75bf22;
  stroke: #ffdc6b;
  stroke-width: 1.5px;
}
polygon.selected {
  fill: #0d75bfaa;
}
.drop-label {
  position: absolute;
  transform: translate(-50%, -50%);
  max-width: 29%;
  padding: 4px;
  border: 1px solid #111;
  color: #111;
  background: #fff;
  font:
    clamp(9px, 3cqw, 13px) / 1.2 Tahoma,
    Arial,
    sans-serif;
  text-align: center;
  white-space: pre-line;
}
.above {
  left: 50%;
  top: 16%;
}
.below {
  left: 50%;
  top: 84%;
}
.left {
  left: 16%;
  top: 50%;
}
.right {
  left: 84%;
  top: 50%;
}
.center {
  left: 50%;
  top: 50%;
}
</style>
