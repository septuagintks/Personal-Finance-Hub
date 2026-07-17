<script setup lang="ts">
import { ArrowLeft, ShieldX } from '@lucide/vue';
import { RouterLink } from 'vue-router';
import AppShell from '../components/AppShell.vue';
</script>

<template>
  <AppShell>
    <section class="access-state" aria-labelledby="forbidden-title">
      <ShieldX :size="34" aria-hidden="true" />
      <p class="eyebrow">403 / Forbidden</p>
      <h1 id="forbidden-title">Operator access required</h1>
      <RouterLink class="button button--quiet" to="/dashboard">
        <ArrowLeft :size="16" /> Return to overview
      </RouterLink>
    </section>
  </AppShell>
</template>
