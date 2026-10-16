<script setup lang="ts">
import { ref } from 'vue'
import type {
  DisplayAnnotations,
  InfoTablePosition,
  MarkerInfoLocation,
} from '../../domain/presentation'
import {
  MARKER_INFO_DRAG_TYPE,
  markerInfoDragName,
  type MarkerInfoField,
} from '../../domain/markerReadouts'
import MarkerReadout from './MarkerReadout.vue'

defineProps<{
  fields: MarkerInfoField[]
  position: InfoTablePosition
  annotations: DisplayAnnotations
  canEditDisplay: boolean
  displayRevision?: string
}>()
const emit = defineEmits<{
  select: [value: { name: string; markerId: number }]
  relocate: [value: { name: string; location: MarkerInfoLocation }]
}>()
const dropping = ref(false)
function dragOver(event: DragEvent) {
  if (!event.dataTransfer?.types.includes(MARKER_INFO_DRAG_TYPE)) return
  event.preventDefault()
  event.dataTransfer.dropEffect = 'move'
  dropping.value = true
}
function drop(event: DragEvent) {
  event.preventDefault()
  dropping.value = false
  const name = markerInfoDragName(event)
  if (name) emit('relocate', { name, location: 'table' })
}
</script>

<template>
  <aside
    class="info-table"
    :class="[position, { dropping }]"
    aria-label="Info Table"
    @dragover="canEditDisplay && dragOver($event)"
    @dragleave="dropping = false"
    @drop="canEditDisplay && drop($event)"
  >
    <div class="info-table-fields">
      <MarkerReadout
        v-for="field in fields"
        :key="field.name"
        :inline="true"
        :show-trace-names="true"
        location="table"
        :info-table-visible="true"
        :markers="field.markers"
        :can-edit-display="canEditDisplay"
        :display-revision="displayRevision"
        :font-scale="annotations.fontScale"
        :stimulus-info-visible="annotations.stimulusInfoVisible"
        @select="emit('select', $event)"
        @relocate="emit('relocate', $event)"
      />
      <p v-if="!fields.length" class="empty-info-table">
        将 Marker 信息框拖入此处，或选择 Show Info in Table。
      </p>
    </div>
  </aside>
</template>

<style scoped>
.info-table {
  flex: 0 1 auto;
  min-width: 0;
  min-height: 40px;
  max-height: 35%;
  padding: 3px;
  overflow: auto;
  scrollbar-width: thin;
  scrollbar-color: #667d87 #202c34;
  background: #202c34;
  border-top: 1px solid #667d87;
}
.info-table.left,
.info-table.right {
  flex: 0 1 300px;
  max-width: 40%;
  max-height: none;
  border-top: 0;
}
.info-table.left {
  border-right: 1px solid #667d87;
}
.info-table.right {
  border-left: 1px solid #667d87;
}
.info-table-fields {
  display: flex;
  flex-wrap: wrap;
  align-content: flex-start;
  align-items: flex-start;
  gap: 4px;
}
.left .info-table-fields,
.right .info-table-fields {
  flex-direction: column;
  flex-wrap: nowrap;
}
.dropping {
  outline: 2px solid #77ccef;
  outline-offset: -2px;
}
.empty-info-table {
  margin: 4px;
  color: #acbcc4;
  font-size: 11px;
}
</style>
