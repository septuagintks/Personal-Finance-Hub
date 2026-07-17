<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue';
import { LoaderCircle, ReceiptText, Save } from '@lucide/vue';
import type { Account } from '../services/account-api';
import type { CategoryTree, Tag } from '../services/metadata-api';
import type {
  CreateTransactionRequest,
  Transaction,
  TransactionType,
} from '../services/transaction-api';
import ModalDialog from './ModalDialog.vue';

const props = withDefaults(
  defineProps<{
    open: boolean;
    mode: 'create' | 'correct';
    accounts: Account[];
    categories: CategoryTree[];
    tags: Tag[];
    transaction?: Transaction | null;
    pending?: boolean;
    error?: string;
    fieldErrors?: Record<string, string>;
  }>(),
  {
    transaction: null,
    pending: false,
    error: '',
    fieldErrors: () => ({}),
  },
);

const emit = defineEmits<{ close: []; submit: [value: CreateTransactionRequest] }>();
const localError = ref('');
const form = reactive({
  accountId: 0,
  type: 'expense' as Exclude<TransactionType, 'transfer'>,
  amount: '',
  categoryId: '' as number | '',
  description: '',
  occurredAt: '',
  tagIds: [] as number[],
});

function flattenCategories(items: CategoryTree[]): CategoryTree[] {
  return items.flatMap((item) => [item, ...flattenCategories(item.children)]);
}

function toLocalInput(value: string | undefined): string {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '';
  const local = new Date(date.getTime() - date.getTimezoneOffset() * 60_000);
  return local.toISOString().slice(0, 16);
}

const account = computed(() => props.accounts.find(({ id }) => id === form.accountId));
const categoryBoard = computed(() => (form.type === 'income' ? 'income' : 'expense'));
const categoryOptions = computed(() =>
  flattenCategories(props.categories).filter(
    (category) => !category.isDeleted && category.board === categoryBoard.value,
  ),
);
const activeTags = computed(() => props.tags.filter(({ isDeleted }) => !isDeleted));
const title = computed(() =>
  props.mode === 'create' ? 'Record transaction' : 'Correct transaction',
);

watch(
  () => [props.open, props.mode, props.transaction, props.accounts] as const,
  ([open]) => {
    if (!open) return;
    localError.value = '';
    const source = props.transaction;
    form.accountId = source?.accountId ?? props.accounts[0]?.id ?? 0;
    form.type = source && source.type !== 'transfer' ? source.type : 'expense';
    form.amount = source?.amount ?? '';
    form.categoryId = source?.categoryId ?? '';
    form.description = source?.description ?? '';
    form.occurredAt = toLocalInput(source?.occurredAt);
    form.tagIds = source?.tags.filter(({ isDeleted }) => !isDeleted).map(({ id }) => id) ?? [];
  },
  { immediate: true },
);

watch(categoryBoard, () => {
  if (!categoryOptions.value.some(({ id }) => id === form.categoryId)) form.categoryId = '';
});

function toggleTag(tagId: number, checked: boolean): void {
  form.tagIds = checked
    ? [...new Set([...form.tagIds, tagId])]
    : form.tagIds.filter((id) => id !== tagId);
}

function submit(): void {
  localError.value = '';
  const amount = form.amount.trim();
  if (!form.accountId || !amount || !account.value) {
    localError.value = 'Complete all required fields.';
    return;
  }
  if (!/^-?[0-9]+(?:\.[0-9]+)?$/.test(amount)) {
    localError.value = 'Amount must be a plain decimal value.';
    return;
  }
  if (form.type !== 'adjustment' && amount.startsWith('-')) {
    localError.value = 'Income and expense amounts must be positive.';
    return;
  }
  const occurredAt = form.occurredAt ? new Date(form.occurredAt).toISOString() : null;
  emit('submit', {
    accountId: form.accountId,
    type: form.type,
    amount,
    currencyCode: account.value.currencyCode,
    categoryId: form.categoryId || null,
    description: form.description.trim() || null,
    occurredAt,
    tagIds: form.tagIds,
  });
}
</script>

<template>
  <ModalDialog
    :open="open"
    :title="title"
    width="wide"
    @close="pending ? undefined : emit('close')"
  >
    <form class="transaction-form" novalidate @submit.prevent="submit">
      <div v-if="error || localError" class="form-alert" role="alert">
        {{ error || localError }}
      </div>
      <div class="transaction-form__types" aria-label="Transaction type">
        <button
          v-for="item in [
            { value: 'income', label: 'Income' },
            { value: 'expense', label: 'Expense' },
            { value: 'adjustment', label: 'Adjustment' },
          ] as const"
          :key="item.value"
          type="button"
          :class="{ 'is-active': form.type === item.value }"
          :aria-pressed="form.type === item.value"
          :disabled="pending"
          @click="form.type = item.value"
        >
          {{ item.label }}
        </button>
      </div>
      <div class="transaction-form__grid">
        <label class="field">
          <span>Account</span>
          <select v-model="form.accountId" :disabled="pending" name="transaction-account">
            <option v-for="item in accounts" :key="item.id" :value="item.id">
              {{ item.name }} · {{ item.currencyCode }}
            </option>
          </select>
          <small v-if="fieldErrors.accountId" class="field-error">{{
            fieldErrors.accountId
          }}</small>
        </label>
        <label class="field">
          <span>{{ form.type === 'adjustment' ? 'Signed amount' : 'Amount' }}</span>
          <input
            v-model="form.amount"
            name="transaction-amount"
            inputmode="decimal"
            autocomplete="off"
            maxlength="128"
            :disabled="pending"
          />
          <small v-if="fieldErrors.amount" class="field-error">{{ fieldErrors.amount }}</small>
        </label>
        <label class="field">
          <span>Category</span>
          <select v-model="form.categoryId" :disabled="pending" name="transaction-category">
            <option value="">Uncategorized</option>
            <option v-for="item in categoryOptions" :key="item.id" :value="item.id">
              {{ item.name }}
            </option>
          </select>
          <small v-if="fieldErrors.categoryId" class="field-error">{{
            fieldErrors.categoryId
          }}</small>
        </label>
        <label class="field">
          <span>Occurred at</span>
          <input
            v-model="form.occurredAt"
            name="transaction-time"
            type="datetime-local"
            :disabled="pending"
          />
          <small v-if="fieldErrors.occurredAt" class="field-error">{{
            fieldErrors.occurredAt
          }}</small>
        </label>
        <label class="field transaction-form__description">
          <span>Description</span>
          <textarea
            v-model="form.description"
            name="transaction-description"
            rows="3"
            maxlength="4096"
            :disabled="pending"
          ></textarea>
        </label>
        <fieldset v-if="activeTags.length" class="transaction-tags-fieldset">
          <legend>Tags</legend>
          <label v-for="tag in activeTags" :key="tag.id">
            <input
              type="checkbox"
              :checked="form.tagIds.includes(tag.id)"
              :disabled="pending"
              @change="toggleTag(tag.id, ($event.target as HTMLInputElement).checked)"
            />
            <span>{{ tag.name }}</span>
          </label>
        </fieldset>
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
        <button class="button" type="submit" :disabled="pending || !accounts.length">
          <LoaderCircle v-if="pending" class="spin" :size="17" />
          <Save v-else-if="mode === 'correct'" :size="17" />
          <ReceiptText v-else :size="17" />
          {{ pending ? 'Saving' : mode === 'create' ? 'Record' : 'Create correction' }}
        </button>
      </footer>
    </form>
  </ModalDialog>
</template>
