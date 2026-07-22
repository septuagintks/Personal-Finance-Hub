<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue';
import { LoaderCircle, Save, WalletCards } from '@lucide/vue';
import type { Account, CreateAccountRequest, UpdateAccountRequest } from '../services/account-api';
import type { CurrencyMetadata } from '../services/user-context-api';
import ModalDialog from './ModalDialog.vue';
import { translate, type MessageKey } from '../i18n';

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

const accountTypes: Array<{ value: CreateAccountRequest['type']; label: MessageKey }> = [
  { value: 'cash', label: 'accountForm.cash' },
  { value: 'savings', label: 'accountForm.savings' },
  { value: 'credit', label: 'accountForm.credit' },
  { value: 'digital_wallet', label: 'accountForm.digitalWallet' },
  { value: 'investment', label: 'accountForm.investment' },
  { value: 'crypto', label: 'accountForm.crypto' },
  { value: 'other', label: 'accountForm.other' },
];

const title = computed(() =>
  translate(props.mode === 'create' ? 'accountForm.createTitle' : 'accountForm.editTitle'),
);

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
    localError.value = translate('auth.register.required');
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
    :description="mode === 'create' ? translate('accountForm.createDescription') : undefined"
    width="wide"
    @close="pending ? undefined : emit('close')"
  >
    <form class="account-form" novalidate @submit.prevent="submit">
      <div v-if="error || localError" class="form-alert" role="alert">
        {{ error || localError }}
      </div>
      <div class="account-form__grid">
        <label class="field">
          <span>{{ translate('accountForm.name') }}</span>
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
          <span>{{ translate('accountForm.type') }}</span>
          <select v-model="form.type" name="account-type" :disabled="pending">
            <option v-for="item in accountTypes" :key="item.value" :value="item.value">
              {{ translate(item.label) }}
            </option>
          </select>
          <small v-if="fieldErrors.type" class="field-error">{{ fieldErrors.type }}</small>
        </label>
        <label class="field">
          <span>{{ translate('accountForm.subtype') }}</span>
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
          <span>{{ translate('accountForm.classification') }}</span>
          <select v-model="form.category" name="account-category" :disabled="pending">
            <option v-if="mode === 'create'" :value="null">
              {{ translate('accountForm.automatic') }}
            </option>
            <option value="asset">{{ translate('accountForm.asset') }}</option>
            <option value="liability">{{ translate('accountForm.liability') }}</option>
          </select>
          <small v-if="fieldErrors.category" class="field-error">{{ fieldErrors.category }}</small>
        </label>
        <label class="field">
          <span>{{ translate('accountForm.currency') }}</span>
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
          <span>{{ translate('accountForm.description') }}</span>
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
          {{ translate('common.cancel') }}
        </button>
        <button class="button" type="submit" :disabled="pending">
          <LoaderCircle v-if="pending" class="spin" :size="17" />
          <Save v-else-if="mode === 'edit'" :size="17" />
          <WalletCards v-else :size="17" />
          {{
            pending
              ? translate('common.saving')
              : translate(mode === 'create' ? 'accountForm.createTitle' : 'accountForm.saveChanges')
          }}
        </button>
      </footer>
    </form>
  </ModalDialog>
</template>
