<script setup>
// Shown whenever a ?build= / #build= override is active, so a reader never runs
// an unpublished (unsigned) build without knowing it.
import { liveState, usePublished } from './config.mjs';
</script>

<template>
  <div v-if="liveState.build" class="live-sql-build-notice" role="status">
    <span>
      Using build <code>{{ liveState.build }}</code> — not the published extension
    </span>
    <button type="button" class="live-sql-btn" @click="usePublished">Use published</button>
  </div>
</template>
