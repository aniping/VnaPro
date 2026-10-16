<script setup lang="ts">
defineProps<{ name: string }>();
</script>

<template>
  <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
    <template v-if="name === 'undo' || name === 'redo'">
      <g :transform="name === 'redo' ? 'translate(24 0) scale(-1 1)' : undefined">
        <path d="M8 5 3 10l5 5M3 10h9a7 7 0 0 1 7 7" />
      </g>
    </template>
    <template v-else-if="name === 'zoom'">
      <rect x="3" y="3" width="12" height="12" stroke-dasharray="2 2" />
      <circle cx="13" cy="13" r="5" /><path d="m17 17 4 4" />
    </template>
    <template v-else-if="name === 'zoom-reset'">
      <circle cx="10" cy="10" r="7" /><path d="m15 15 6 6" />
      <text x="10" y="12" class="small-text">1:1</text>
    </template>
    <template v-else-if="name === 'layout'">
      <rect x="3" y="3" width="18" height="18" /><path d="M12 3v18M3 12h18" />
    </template>
    <template v-else-if="name === 'maximize'">
      <path d="M9 3H3v6M15 3h6v6M3 15v6h6M21 15v6h-6M3 3l6 6m12-6-6 6M3 21l6-6m12 6-6-6" />
    </template>
    <template v-else-if="name === 'add-trace'">
      <path d="M3 17h3l3-9 4 11 3-7h2M18 3v8M14 7h8" />
    </template>
    <template v-else-if="name === 'marker'">
      <path d="M2 18h4l4-8 4 9 6-8M10 3v5" /><path class="glyph-fill" d="m7 3 3 4 3-4Z" />
    </template>
    <template v-else-if="name === 'add-channel'">
      <path d="M3 4h11v16H3ZM3 9h11M18 11v10M13 16h10" />
    </template>
    <template v-else-if="name === 'delete'">
      <path d="M5 7h14M9 4h6M7 7l1 13h8l1-13M10 10v7M14 10v7" />
    </template>
    <template v-else-if="name === 'export'">
      <path d="M5 3h10l4 4v14H5ZM15 3v5h4M12 10v7m-3-3 3 3 3-3" />
    </template>
    <template v-else-if="name === 'restart'">
      <path d="M3 17h4l3-9 4 10 4-7h3M6 4h8" /><path class="glyph-fill" d="m15 2 4 2-4 2Z" />
    </template>
    <template v-else-if="name === 'autoscale'">
      <path d="M4 3v18h17M8 8l3-4 3 4M11 4v13m-3-4 3 4 3-4M15 10l3 3 3-3" />
    </template>
    <template v-else-if="name === 'settings'">
      <path d="M4 6h16M4 12h16M4 18h16" />
      <rect x="7" y="3" width="4" height="6" class="glyph-fill" />
      <rect x="14" y="9" width="4" height="6" class="glyph-fill" />
      <rect x="8" y="15" width="4" height="6" class="glyph-fill" />
    </template>
    <template v-else-if="name === 'rf'">
      <text x="10" y="15" class="label-text">RF</text><path d="M19 3v5m-2 0a4 4 0 1 0 4 0" />
    </template>
    <template v-else-if="name === 'panel'">
      <path d="m9 7 5 5-5 5m6-10 5 5-5 5" />
    </template>
    <template v-else-if="name === 'camera'">
      <path d="M3 7h5l2-3h4l2 3h5v13H3Z" /><circle cx="12" cy="13" r="4" />
    </template>
  </svg>
</template>

<style scoped>
svg { width: 23px; height: 23px; fill: none; stroke: currentColor; stroke-width: 1.6; stroke-linecap: round; stroke-linejoin: round; }
.glyph-fill { fill: currentColor; stroke: none; }
.small-text, .label-text { fill: currentColor; stroke: none; text-anchor: middle; font: 600 6px "Segoe UI", sans-serif; }
.label-text { font-size: 9px; }
</style>
