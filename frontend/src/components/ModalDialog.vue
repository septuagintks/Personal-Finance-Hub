<script setup lang="ts">
import { nextTick, onBeforeUnmount, ref, useId, watch } from 'vue';
import { X } from '@lucide/vue';
import { translate } from '../i18n';

const props = defineProps<{
  open: boolean;
  title: string;
  description?: string;
  width?: 'standard' | 'wide';
}>();

const emit = defineEmits<{ close: [] }>();
const dialog = ref<HTMLDialogElement | null>(null);
const titleId = useId();

watch(
  () => props.open,
  async (open) => {
    await nextTick();
    const element = dialog.value;
    if (!element) return;
    if (open && !element.open) element.showModal();
    if (!open && element.open) element.close();
  },
  { immediate: true },
);

function cancel(event: Event): void {
  event.preventDefault();
  emit('close');
}

function backdrop(event: MouseEvent): void {
  if (event.target === dialog.value) emit('close');
}

onBeforeUnmount(() => {
  if (dialog.value?.open) dialog.value.close();
});
</script>

<template>
  <dialog
    ref="dialog"
    class="modal-dialog"
    :class="{ 'modal-dialog--wide': width === 'wide' }"
    :aria-labelledby="titleId"
    @cancel="cancel"
    @click="backdrop"
  >
    <div class="modal-dialog__surface">
      <header class="modal-dialog__header">
        <div>
          <h2 :id="titleId">{{ title }}</h2>
          <p v-if="description">{{ description }}</p>
        </div>
        <button
          class="icon-button"
          type="button"
          :aria-label="translate('common.closeDialog')"
          :title="translate('common.closeDialog')"
          @click="emit('close')"
        >
          <X :size="19" />
        </button>
      </header>
      <div class="modal-dialog__body"><slot /></div>
    </div>
  </dialog>
</template>
