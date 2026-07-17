<script setup lang="ts">
import { watch } from 'vue';
import { RouterView, useRoute, useRouter } from 'vue-router';
import { brand } from './app/brand';
import { useSessionStore } from './stores/session';

document.title = brand.name;
const route = useRoute();
const router = useRouter();
const session = useSessionStore();

watch(
  () => session.status,
  async (status) => {
    if (status === 'anonymous' && route.meta.requiresAuth) {
      await router.replace({ name: 'login', query: { redirect: route.fullPath } });
    }
  },
);

watch(
  () => [session.isOperator, route.meta.requiresOperator] as const,
  async ([isOperator, requiresOperator]) => {
    if (requiresOperator && session.isAuthenticated && !isOperator) {
      await router.replace({ name: 'forbidden' });
    }
  },
);
</script>

<template>
  <RouterView />
</template>
