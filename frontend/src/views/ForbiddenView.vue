<script setup lang="ts">
import { ArrowLeft, ShieldX } from '@lucide/vue';
import { RouterLink } from 'vue-router';
import AppShell from '../components/AppShell.vue';
import { translate } from '../i18n';
</script>

<template>
  <AppShell>
    <section class="access-state" aria-labelledby="forbidden-title">
      <ShieldX :size="34" aria-hidden="true" />
      <p class="eyebrow">{{ translate('route.forbiddenEyebrow') }}</p>
      <h1 id="forbidden-title">{{ translate('route.forbiddenTitle') }}</h1>
      <RouterLink class="button button--quiet" to="/dashboard">
        <ArrowLeft :size="16" /> {{ translate('route.returnOverview') }}
      </RouterLink>
    </section>
  </AppShell>
</template>
