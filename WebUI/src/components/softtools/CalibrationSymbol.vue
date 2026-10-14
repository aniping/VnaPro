<script setup lang="ts">
defineProps<{
  kind:
    | 'port'
    | 'reflection'
    | 'transmission'
    | 'both'
    | 'onePath'
    | 'full'
    | 'unavailable'
    | 'check'
    | 'next'
    | 'previous'
    | 'start'
    | 'cancel'
}>()
</script>
<template>
  <svg
    :viewBox="kind === 'port' ? '0 0 24 24' : '0 0 48 48'"
    aria-hidden="true"
    focusable="false"
  >
    <template v-if="kind === 'port'">
      <circle cx="12" cy="12" r="8" fill="currentColor" />
      <circle cx="12" cy="12" r="3" class="port-hole" />
    </template>
    <template v-else-if="kind === 'reflection'">
      <path
        d="M18 7c13 4 17 18 5 28"
        fill="none"
        stroke="currentColor"
        stroke-width="4"
      />
      <path d="m17 29 11 6-12 6Z" fill="currentColor" />
    </template>
    <template v-else-if="kind === 'transmission' || kind === 'both'">
      <path d="M5 17h31" stroke="currentColor" stroke-width="4" />
      <path d="m34 11 11 6-11 6Z" fill="currentColor" />
      <template v-if="kind === 'both'">
        <path d="M43 31H12" stroke="currentColor" stroke-width="4" />
        <path d="m14 25-11 6 11 6Z" fill="currentColor" />
      </template>
    </template>
    <template v-else-if="kind === 'full' || kind === 'onePath'">
      <path
        d="M5 7h31M8 14c10 4 10 15 0 20"
        fill="none"
        stroke="currentColor"
        stroke-width="3"
      />
      <path d="m34 1 11 6-11 6ZM3 30l11 5-11 6Z" fill="currentColor" />
      <template v-if="kind === 'full'">
        <path
          d="M43 41H12M40 34c-10-4-10-15 0-20"
          fill="none"
          stroke="currentColor"
          stroke-width="3"
        />
        <path d="m14 35-11 6 11 6ZM45 18l-11-5 11-6Z" fill="currentColor" />
      </template>
    </template>
    <template v-else-if="kind === 'unavailable'">
      <circle
        cx="24"
        cy="24"
        r="18"
        fill="none"
        stroke="currentColor"
        stroke-width="3"
      />
      <path d="m12 12 24 24" stroke="currentColor" stroke-width="3" />
    </template>
    <path
      v-else-if="kind === 'check'"
      d="m8 24 10 10 23-23"
      fill="none"
      stroke="currentColor"
      stroke-width="5"
    />
    <path
      v-else-if="kind === 'next'"
      d="m15 8 20 16-20 16Z"
      fill="currentColor"
    />
    <path
      v-else-if="kind === 'previous'"
      d="m33 8-20 16 20 16Z"
      fill="currentColor"
    />
    <template v-else-if="kind === 'start'">
      <circle
        cx="24"
        cy="24"
        r="19"
        fill="none"
        stroke="currentColor"
        stroke-width="2.5"
      />
      <path d="m18 12 17 12-17 12Z" fill="currentColor" />
    </template>
    <path
      v-else
      d="m10 10 28 28M38 10 10 38"
      fill="none"
      stroke="currentColor"
      stroke-width="5"
    />
  </svg>
</template>
<style scoped>
svg {
  display: block;
  width: 100%;
  height: 100%;
}
.port-hole {
  fill: var(--port-fill, #0099f0);
}
</style>
