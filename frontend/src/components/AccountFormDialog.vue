<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue';
import { LoaderCircle, Save, WalletCards } from '@lucide/vue';
import type { Account, CreateAccountRequest, UpdateAccountRequest } from '../services/account-api';
import type { CurrencyMetadata } from '../services/user-context-api';
import ModalDialog from './ModalDialog.vue';

export type AccountFormValue = Omit<UpdateAccountRequest, 'category'> & {
  category: UpdateAccountRequest['category'] | null;
};

const props = withDefaults(
  defineProps<{
    open: boolean;
    mode: 'create' | 'edit';
    account?: Account | null;
    currencies: CurrencyMetadata[];
    defaultCurrency?: string;
    pending?: boolean;
    error?: string;
    fieldErrors?: Record<string, string>;
  }>(),
  {
    account: null,
    defaultCurrency: '',
    pending: false,
    error: '',
    fieldErrors: () => ({}),
  },
);

const emit = defineEmits<{ close: []; submit: [value: AccountFormValue] }>();
const localError = ref('');
const form = reactive<AccountFormValue>({
  name: '',
  type: 'cash',
  subtype: '',
  category: 'asset',
  currencyCode: 'CNY',
  description: '',
});

const accountTypes: Array<{ value: CreateAccountRequest['type']; label: string }> = [
  { value: 'cash', label: 'Cash' },
  { value: 'savings', label: 'Savings' },
  { value: 'credit', label: 'Credit' },
  { value: 'digital_wallet', label: 'Digital wallet' },
  { value: 'investment', label: 'Investment' },
  { value: 'crypto', label: 'Crypto' },
  { value: 'other', label: 'Other' },
];

const title = computed(() => (props.mode === 'create' ? 'Create account' : 'Edit account'));

watch(
  () => [props.open, props.mode, props.account, props.currencies, props.defaultCurrency] as const,
  ([open]) => {
    if (!open) return;
    localError.value = '';
    form.name = props.account?.name ?? '';
    form.type = (props.account?.type as CreateAccountRequest['type'] | undefined) ?? 'cash';
    form.subtype = props.account?.subtype ?? '';
    form.category = props.account?.category ?? null;
    const preferredCurrency = props.currencies.some(({ code }) => code === props.defaultCurrency)
      ? props.defaultCurrency
      : props.currencies[0]?.code;
    form.currencyCode = props.account?.currencyCode ?? preferredCurrency ?? 'CNY';
    form.description = props.account?.description ?? '';
  },
  { immediate: true },
);

function submit(): void {
  localError.value = '';
  if (!form.name.trim() || !form.subtype.trim() || !form.currencyCode) {
    localError.value = 'Complete all required fields.';
    return;
  }
  emit('submit', {
    name: form.name.trim(),
    type: form.type,
    subtype: form.subtype.trim(),
    category: form.category,
    currencyCode: form.currencyCode,
    description: form.description.trim(),
  });
}
</script>

<template>
  <ModalDialog
    :open="open"
    :title="title"
    :description="mode === 'create' ? 'Add a place where money is held or owed.' : undefined"
    width="wide"
    @close="pending ? undefined : emit('close')"
  >
    <form class="account-form" novalidate @submit.prevent="submit">
      <div v-if="error || localError" class="form-alert" role="alert">
        {{ error || localError }}
      </div>
      <div class="account-form__grid">
        <label class="field">
          <span>Name</span>
          <input
            v-model="form.name"
            name="account-name"
            autocomplete="off"
            maxlength="128"
            :disabled="pending"
          />
          <small v-if="fieldErrors.name" class="field-error">{{ fieldErrors.name }}</small>
        </label>
        <label class="field">
          <span>Type</span>
          <select v-model="form.type" name="account-type" :disabled="pending">
            <option v-for="item in accountTypes" :key="item.value" :value="item.value">
              {{ item.label }}
            </option>
          </select>
          <small v-if="fieldErrors.type" class="field-error">{{ fieldErrors.type }}</small>
        </label>
        <label class="field">
          <span>Subtype</span>
          <input
            v-model="form.subtype"
            name="account-subtype"
            autocomplete="off"
            maxlength="64"
            :disabled="pending"
          />
          <small v-if="fieldErrors.subtype" class="field-error">{{ fieldErrors.subtype }}</small>
        </label>
        <label class="field">
          <span>Classification</span>
          <select v-model="form.category" name="account-category" :disabled="pending">
            <option v-if="mode === 'create'" :value="null">Automatic</option>
            <option value="asset">Asset</option>
            <option value="liability">Liability</option>
          </select>
          <small v-if="fieldErrors.category" class="field-error">{{ fieldErrors.category }}</small>
        </label>
        <label class="field">
          <span>Currency</span>
          <select v-model="form.currencyCode" name="account-currency" :disabled="pending">
            <option v-for="currency in currencies" :key="currency.code" :value="currency.code">
              {{ currency.code }} · {{ currency.displayName }}
            </option>
          </select>
          <small v-if="fieldErrors.currencyCode" class="field-error">{{
            fieldErrors.currencyCode
          }}</small>
        </label>
        <label class="field account-form__description">
          <span>Description</span>
          <textarea
            v-model="form.description"
            name="account-description"
            maxlength="4096"
            rows="3"
            :disabled="pending"
          ></textarea>
          <small v-if="fieldErrors.description" class="field-error">{{
            fieldErrors.description
          }}</small>
        </label>
      </div>
      <footer class="modal-actions">
        <button
          class="button button--quiet"
          type="button"
          :disabled="pending"
          @click="emit('close')"
        >
          Cancel
        </button>
        <button class="button" type="submit" :disabled="pending">
          <LoaderCircle v-if="pending" class="spin" :size="17" />
          <Save v-else-if="mode === 'edit'" :size="17" />
          <WalletCards v-else :size="17" />
          {{ pending ? 'Saving' : mode === 'create' ? 'Create account' : 'Save changes' }}
        </button>
      </footer>
    </form>
  </ModalDialog>
</template>
