<script setup lang="ts">
import { onMounted, reactive, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { CircleAlert, Filter, LoaderCircle, Plus, ReceiptText, RotateCcw } from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import TransactionFormDialog from '../components/TransactionFormDialog.vue';
import { ApiError, type ApiErrorShape } from '../services/api-error';
import { formatDecimalString, formatInstant } from '../services/presentation';
import type { CreateTransactionRequest, TransactionFilters } from '../services/transaction-api';
import { useAccountStore } from '../stores/accounts';
import { useMetadataStore } from '../stores/metadata';
import { useTransactionStore } from '../stores/transactions';
import { useUserContextStore } from '../stores/user-context';

const route = useRoute();
const router = useRouter();
const accounts = useAccountStore();
const metadata = useMetadataStore();
const transactions = useTransactionStore();
const userContext = useUserContextStore();
const createOpen = ref(false);
const createPending = ref(false);
const createError = ref('');
const pageError = ref<ApiErrorShape | null>(null);
const draft = reactive({
  accountId: '',
  type: '',
  categoryId: '',
  tagId: '',
  keyword: '',
  from: '',
  to: '',
});

const typeLabels: Record<string, string> = {
  income: 'Income',
  expense: 'Expense',
  transfer: 'Transfer',
  adjustment: 'Adjustment',
};

function queryString(name: string): string {
  const value = route.query[name];
  return typeof value === 'string' ? value : '';
}

function positiveId(value: string): number | undefined {
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) && parsed > 0 ? parsed : undefined;
}

function dateBoundary(value: string, end: boolean): string | undefined {
  if (!value) return undefined;
  const date = new Date(`${value}T00:00:00`);
  if (end) date.setDate(date.getDate() + 1);
  return Number.isNaN(date.getTime()) ? undefined : date.toISOString();
}

function filtersFromRoute(): TransactionFilters {
  const type = queryString('type');
  return {
    accountId: positiveId(queryString('accountId')),
    type: ['income', 'expense', 'transfer', 'adjustment'].includes(type)
      ? (type as TransactionFilters['type'])
      : undefined,
    categoryId: positiveId(queryString('categoryId')),
    tagId: positiveId(queryString('tagId')),
    keyword: queryString('keyword') || undefined,
    from: dateBoundary(queryString('from'), false),
    to: dateBoundary(queryString('to'), true),
    pageSize: 50,
  };
}

function syncDraft(): void {
  draft.accountId = queryString('accountId');
  draft.type = queryString('type');
  draft.categoryId = queryString('categoryId');
  draft.tagId = queryString('tagId');
  draft.keyword = queryString('keyword');
  draft.from = queryString('from');
  draft.to = queryString('to');
}

function mapError(error: unknown): ApiErrorShape {
  if (error instanceof ApiError) return error.details;
  return {
    status: 0,
    errorCode: 'UNKNOWN_ERROR',
    message: 'Transactions could not be loaded.',
    traceId: '',
    fieldErrors: {},
    retryable: true,
  };
}

async function load(): Promise<void> {
  pageError.value = null;
  try {
    await transactions.load(filtersFromRoute());
  } catch (error) {
    pageError.value = mapError(error);
  }
}

async function applyFilters(): Promise<void> {
  const query = Object.fromEntries(Object.entries(draft).filter(([, value]) => value !== ''));
  await router.replace({ name: 'transactions', query });
}

async function clearFilters(): Promise<void> {
  await router.replace({ name: 'transactions' });
}

async function loadMore(): Promise<void> {
  pageError.value = null;
  try {
    await transactions.loadMore();
  } catch (error) {
    pageError.value = mapError(error);
  }
}

function accountName(accountId: number): string {
  return accounts.items.find(({ id }) => id === accountId)?.name ?? `Account ${accountId}`;
}

function amountText(amount: string): string {
  return formatDecimalString(amount, userContext.preference?.locale ?? 'en-US');
}

function occurredText(value: string): string {
  return formatInstant(value, {
    locale: userContext.preference?.locale ?? 'en-US',
    timeZone: userContext.preference?.timezone ?? 'UTC',
    dateFormat: userContext.preference?.dateFormat,
  });
}

function handleLedgerKeydown(event: KeyboardEvent): void {
  if (event.key !== 'ArrowRight' && event.key !== 'ArrowLeft') return;
  const element = event.currentTarget;
  if (!(element instanceof HTMLElement) || element.scrollWidth <= element.clientWidth) return;
  event.preventDefault();
  const distance = Math.max(40, Math.round(element.clientWidth * 0.75));
  element.scrollLeft += event.key === 'ArrowRight' ? distance : -distance;
}

async function submitCreate(payload: CreateTransactionRequest): Promise<void> {
  createPending.value = true;
  createError.value = '';
  try {
    const result = await transactions.create(payload);
    createOpen.value = false;
    await router.push({ name: 'transaction-detail', params: { transactionId: result.id } });
  } catch (error) {
    createError.value = mapError(error).message;
  } finally {
    createPending.value = false;
  }
}

watch(
  () => route.fullPath,
  () => {
    syncDraft();
    void load();
  },
);

onMounted(async () => {
  syncDraft();
  await Promise.all([
    accounts.loadList('active'),
    metadata.loadCategories(),
    metadata.loadTags(),
  ]).catch(() => undefined);
  await load();
});
</script>

<template>
  <AppShell>
    <div class="content-header">
      <div>
        <p class="eyebrow">Ledger / Transactions</p>
        <h1>Transactions</h1>
        <p class="content-header__copy">The ordered financial record across your accounts.</p>
      </div>
      <button
        class="button"
        type="button"
        :disabled="!accounts.items.length"
        @click="createOpen = true"
      >
        <Plus :size="17" /> Record transaction
      </button>
    </div>

    <form class="ledger-filters" aria-label="Transaction filters" @submit.prevent="applyFilters">
      <Filter :size="17" aria-hidden="true" />
      <label>
        <span>Account</span>
        <select v-model="draft.accountId">
          <option value="">All accounts</option>
          <option v-for="account in accounts.items" :key="account.id" :value="String(account.id)">
            {{ account.name }}
          </option>
        </select>
      </label>
      <label>
        <span>Type</span>
        <select v-model="draft.type">
          <option value="">All types</option>
          <option v-for="(label, value) in typeLabels" :key="value" :value="value">
            {{ label }}
          </option>
        </select>
      </label>
      <label>
        <span>Category</span>
        <select v-model="draft.categoryId">
          <option value="">All categories</option>
          <template v-for="root in metadata.categories" :key="root.id">
            <option :value="String(root.id)">{{ root.name }}</option>
            <option v-for="child in root.children" :key="child.id" :value="String(child.id)">
              {{ child.name }}
            </option>
          </template>
        </select>
      </label>
      <label>
        <span>Tag</span>
        <select v-model="draft.tagId">
          <option value="">All tags</option>
          <option v-for="tag in metadata.tags" :key="tag.id" :value="String(tag.id)">
            {{ tag.name }}
          </option>
        </select>
      </label>
      <label class="ledger-filters__search">
        <span>Search</span>
        <input v-model="draft.keyword" maxlength="128" type="search" />
      </label>
      <label>
        <span>From</span>
        <input v-model="draft.from" type="date" />
      </label>
      <label>
        <span>To</span>
        <input v-model="draft.to" type="date" />
      </label>
      <div class="ledger-filters__actions">
        <button
          class="icon-button"
          type="button"
          title="Clear filters"
          aria-label="Clear filters"
          @click="clearFilters"
        >
          <RotateCcw :size="17" />
        </button>
        <button class="button button--small" type="submit">Apply</button>
      </div>
    </form>

    <div v-if="pageError" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>Ledger unavailable</strong>
        <p>{{ pageError.message }}</p>
      </div>
      <button
        v-if="pageError.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load"
      >
        Try again
      </button>
    </div>

    <section
      class="ledger-list"
      role="region"
      aria-label="Transaction results"
      aria-live="polite"
      :aria-busy="transactions.listState === 'loading'"
      tabindex="0"
      @keydown="handleLedgerKeydown"
    >
      <div class="ledger-list__head" aria-hidden="true">
        <span>When</span><span>Entry</span><span>Account</span><span>Classification</span
        ><span>Amount</span>
      </div>
      <div v-if="transactions.listState === 'loading'" class="account-list__loading">
        <span v-for="index in 5" :key="index"></span>
      </div>
      <template v-else-if="transactions.items.length">
        <RouterLink
          v-for="transaction in transactions.items"
          :key="transaction.id"
          class="ledger-list__row"
          :to="{ name: 'transaction-detail', params: { transactionId: transaction.id } }"
        >
          <time :datetime="transaction.occurredAt">{{ occurredText(transaction.occurredAt) }}</time>
          <span class="ledger-entry">
            <strong>{{ transaction.description || typeLabels[transaction.type] }}</strong>
            <span class="ledger-tags">
              <small v-for="tag in transaction.tags" :key="tag.id">{{ tag.name }}</small>
            </span>
          </span>
          <span data-label="Account">{{ accountName(transaction.accountId) }}</span>
          <span data-label="Classification">
            <span class="transaction-type" :class="`transaction-type--${transaction.type}`">{{
              typeLabels[transaction.type]
            }}</span>
            <small v-if="transaction.categoryName">{{ transaction.categoryName }}</small>
          </span>
          <strong class="ledger-amount" :class="`ledger-amount--${transaction.type}`">
            {{ amountText(transaction.amount) }} <small>{{ transaction.currencyCode }}</small>
          </strong>
        </RouterLink>
      </template>
      <div v-else class="account-empty">
        <ReceiptText :size="28" />
        <strong>No transactions match this view.</strong>
      </div>
    </section>
    <div v-if="transactions.nextCursor" class="ledger-load-more">
      <button
        class="button button--quiet"
        type="button"
        :disabled="transactions.listState === 'loading-more'"
        @click="loadMore"
      >
        <LoaderCircle v-if="transactions.listState === 'loading-more'" class="spin" :size="17" />
        Load more
      </button>
    </div>

    <TransactionFormDialog
      :open="createOpen"
      mode="create"
      :accounts="accounts.items"
      :categories="metadata.categories"
      :tags="metadata.tags"
      :pending="createPending"
      :error="createError"
      @close="createOpen = false"
      @submit="submitCreate"
    />
  </AppShell>
</template>
