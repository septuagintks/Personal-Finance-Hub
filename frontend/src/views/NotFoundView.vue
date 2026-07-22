<script setup lang="ts">
import { ArrowLeft } from '@lucide/vue';
import { RouterLink } from 'vue-router';
import BrandMark from '../components/BrandMark.vue';
import { translate } from '../i18n';
</script>

<template>
  <main class="simple-page">
    <BrandMark />
    <p class="eyebrow">{{ translate('route.notFoundEyebrow') }}</p>
    <h1>{{ translate('route.notFoundTitle') }}</h1>
    <p>{{ translate('route.notFoundDescription') }}</p>
    <RouterLink class="button" to="/"
      ><ArrowLeft :size="17" /> {{ translate('route.returnOverview') }}</RouterLink
    >
  </main>
</template>
