<script setup lang="ts">
import { computed } from 'vue';
import { Languages } from '@lucide/vue';
import { currentLocale, setApplicationLocale, translate } from '../i18n';

const locale = computed(() => currentLocale());

async function change(event: Event): Promise<void> {
  const target = event.target as HTMLSelectElement;
  await setApplicationLocale(target.value, { persistPublic: true });
}
</script>

<template>
  <label class="locale-select">
    <Languages :size="16" aria-hidden="true" />
    <span class="sr-only">{{ translate('common.language') }}</span>
    <select :value="locale" :aria-label="translate('common.language')" @change="change">
      <option value="en-US">{{ translate('common.english') }}</option>
      <option value="zh-CN">{{ translate('common.simplifiedChinese') }}</option>
    </select>
  </label>
</template>
