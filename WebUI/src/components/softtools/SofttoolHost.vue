<script setup lang="ts">
defineProps<{
  title: string
  tabs: { id: string; label: string; disabled?: boolean; reason?: string }[]
  activeTab: string
}>()
const emit = defineEmits<{ select: [id: string]; close: [] }>()
</script>

<template>
  <aside class="softtool-host" aria-label="Softtool panel">
    <header class="softtool-header">
      <h1>{{ title }}</h1>
      <button
        type="button"
        aria-label="Close softtool"
        title="Close · Esc"
        @click="emit('close')"
      >
        ×
      </button>
    </header>
    <div class="softtool-body">
      <nav class="softtool-tabs" aria-label="Softtool tabs">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          type="button"
          :class="{ 'is-active': activeTab === tab.id }"
          :aria-pressed="activeTab === tab.id"
          :disabled="tab.disabled"
          :title="tab.reason"
          @click="emit('select', tab.id)"
        >
          {{ tab.label }}
        </button>
      </nav>
      <div class="softtool-content"><slot /></div>
    </div>
  </aside>
</template>
