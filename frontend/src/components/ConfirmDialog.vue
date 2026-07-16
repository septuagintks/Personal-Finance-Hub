<script setup lang="ts">
import { Archive, LoaderCircle, RotateCcw } from '@lucide/vue';
import ModalDialog from './ModalDialog.vue';

const props = withDefaults(
  defineProps<{
    open: boolean;
    title: string;
    description: string;
    confirmLabel: string;
    pending?: boolean;
    action?: 'archive' | 'restore';
  }>(),
  { pending: false, action: 'archive' },
);

const emit = defineEmits<{ close: []; confirm: [] }>();
</script>

<template>
  <ModalDialog
    :open="open"
    :title="title"
    :description="description"
    @close="pending ? undefined : emit('close')"
  >
    <div class="confirmation-detail"><slot /></div>
    <footer class="modal-actions">
      <button class="button button--quiet" type="button" :disabled="pending" @click="emit('close')">
        Cancel
      </button>
      <button class="button" type="button" :disabled="pending" @click="emit('confirm')">
        <LoaderCircle v-if="pending" class="spin" :size="17" />
        <RotateCcw v-else-if="props.action === 'restore'" :size="17" />
        <Archive v-else :size="17" />
        {{ pending ? 'Applying' : confirmLabel }}
      </button>
    </footer>
  </ModalDialog>
</template>
