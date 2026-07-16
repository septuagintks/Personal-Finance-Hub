<script setup lang="ts">
import { ArrowLeft } from '@lucide/vue';
import { RouterLink } from 'vue-router';
import BrandMark from '../components/BrandMark.vue';
</script>

<template>
  <main class="simple-page">
    <BrandMark />
    <p class="eyebrow">404 / Not found</p>
    <h1>This page is not in the ledger.</h1>
    <p>The address may have moved or never existed.</p>
    <RouterLink class="button" to="/"><ArrowLeft :size="17" /> Return to overview</RouterLink>
  </main>
</template>
