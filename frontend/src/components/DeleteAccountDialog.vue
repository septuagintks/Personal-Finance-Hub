<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { ArrowLeft, ArrowRight, LoaderCircle, Trash2 } from '@lucide/vue';
import type { Account } from '../services/account-api';
import ModalDialog from './ModalDialog.vue';
import { translate } from '../i18n';

const props = withDefaults(
  defineProps<{
    open: boolean;
    account: Account;
    pending?: boolean;
    error?: string;
  }>(),
  { pending: false, error: '' },
);

const emit = defineEmits<{ close: []; confirm: [] }>();
const step = ref(1);
const acknowledged = ref(false);
const typedName = ref('');
const nameMatches = computed(() => typedName.value === props.account.name);

watch(
  () => props.open,
  (open) => {
    if (!open) return;
    step.value = 1;
    acknowledged.value = false;
    typedName.value = '';
  },
);
</script>

<template>
  <ModalDialog
    :open="open"
    :title="translate('deleteAccount.title')"
    :description="translate('deleteAccount.step', { step })"
    @close="pending ? undefined : emit('close')"
  >
    <div v-if="error" class="form-alert" role="alert">{{ error }}</div>
    <section v-if="step === 1" class="delete-step">
      <h3>{{ translate('deleteAccount.reviewTitle') }}</h3>
      <ul class="impact-list">
        <li>{{ translate('deleteAccount.transactions') }}</li>
        <li>{{ translate('deleteAccount.transfers') }}</li>
        <li>{{ translate('deleteAccount.relatedData') }}</li>
      </ul>
    </section>
    <section v-else-if="step === 2" class="delete-step">
      <h3>{{ translate('deleteAccount.confirmTitle') }}</h3>
      <label class="acknowledgement">
        <input v-model="acknowledged" type="checkbox" />
        <span>{{ translate('deleteAccount.acknowledge') }}</span>
      </label>
    </section>
    <section v-else class="delete-step">
      <h3>{{ translate('deleteAccount.enterName') }}</h3>
      <label class="field">
        <span>{{ account.name }}</span>
        <input
          v-model="typedName"
          name="delete-account-name"
          autocomplete="off"
          :disabled="pending"
        />
      </label>
    </section>
    <footer class="modal-actions modal-actions--split">
      <button
        class="button button--quiet"
        type="button"
        :disabled="pending"
        @click="step === 1 ? emit('close') : (step -= 1)"
      >
        <ArrowLeft v-if="step > 1" :size="17" />
        {{ translate(step === 1 ? 'common.cancel' : 'common.back') }}
      </button>
      <button
        v-if="step < 3"
        class="button"
        type="button"
        :disabled="step === 2 && !acknowledged"
        @click="step += 1"
      >
        {{ translate('common.continue') }} <ArrowRight :size="17" />
      </button>
      <button
        v-else
        class="button button--danger"
        type="button"
        :disabled="pending || !nameMatches"
        @click="emit('confirm')"
      >
        <LoaderCircle v-if="pending" class="spin" :size="17" />
        <Trash2 v-else :size="17" />
        {{ translate(pending ? 'deleteAccount.deleting' : 'deleteAccount.deletePermanently') }}
      </button>
    </footer>
  </ModalDialog>
</template>
