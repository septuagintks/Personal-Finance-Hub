<script setup lang="ts">
import { ArrowLeft } from '@lucide/vue';
import { RouterLink } from 'vue-router';
import BrandMark from './BrandMark.vue';

defineProps<{
  eyebrow: string;
  title: string;
  description: string;
  alternateText: string;
  alternateLabel: string;
  alternateTo: string;
}>();
</script>

<template>
  <main class="auth-page">
    <div class="auth-page__topbar">
      <RouterLink class="back-link" to="/"><ArrowLeft :size="16" /> Back to overview</RouterLink>
      <BrandMark compact />
    </div>
    <section class="auth-layout" aria-labelledby="auth-title">
      <div class="auth-intro">
        <p class="eyebrow">{{ eyebrow }}</p>
        <h1 id="auth-title">{{ title }}</h1>
        <p class="auth-intro__copy">{{ description }}</p>
        <div class="auth-rule" aria-hidden="true"><span></span><span></span><span></span></div>
        <p class="auth-intro__note">
          Private by default. Your browser keeps access credentials in memory only.
        </p>
      </div>
      <div class="auth-panel">
        <slot />
        <p class="auth-switch">
          {{ alternateText }} <RouterLink :to="alternateTo">{{ alternateLabel }}</RouterLink>
        </p>
      </div>
    </section>
  </main>
</template>
