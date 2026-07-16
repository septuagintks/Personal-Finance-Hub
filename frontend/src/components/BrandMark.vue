<script setup lang="ts">
import { CircleDollarSign } from '@lucide/vue';
import { brand } from '../app/brand';

withDefaults(defineProps<{ compact?: boolean }>(), { compact: false });
</script>

<template>
  <div class="brand-lockup" :class="{ 'brand-lockup--compact': compact }">
    <span class="brand-mark" aria-hidden="true"
      ><CircleDollarSign :size="compact ? 18 : 22" stroke-width="1.8"
    /></span>
    <span class="brand-name">{{ brand.name }}</span>
  </div>
</template>
