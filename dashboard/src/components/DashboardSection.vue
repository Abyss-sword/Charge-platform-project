<script setup lang="ts">
defineProps<{ id: string; title: string; source: string }>()
</script>

<template>
  <section :id="id" class="dashboard-region" :aria-labelledby="`${id}-title`">
    <header class="region-header">
      <div><span class="eyebrow">DATA DOMAIN</span><h2 :id="`${id}-title`">{{ title }}</h2></div>
      <span class="region-source">{{ source }}</span>
    </header>
    <slot />
  </section>
</template>
