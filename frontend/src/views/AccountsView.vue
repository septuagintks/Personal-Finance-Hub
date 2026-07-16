<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import { Archive, CircleAlert, Filter, Landmark, Plus, RefreshCw, WalletCards } from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import AccountFormDialog, { type AccountFormValue } from '../components/AccountFormDialog.vue';
import { ApiError, type ApiErrorShape } from '../services/api-error';
import type { AccountListStatus } from '../services/account-api';
import { useAccountStore } from '../stores/accounts';
import { useUserContextStore } from '../stores/user-context';

const router = useRouter();
const accounts = useAccountStore();
const userContext = useUserContextStore();
const status = ref<AccountListStatus>('active');
const typeFilter = ref('all');
const currencyFilter = ref('all');
const createOpen = ref(false);
const createPending = ref(false);
const createError = ref('');
const createFieldErrors = ref<Record<string, string>>({});
const pageError = ref<ApiErrorShape | null>(null);

const accountTypeLabels: Record<string, string> = {
  cash: 'Cash',
  savings: 'Savings',
  credit: 'Credit',
  digital_wallet: 'Digital wallet',
  investment: 'Investment',
  crypto: 'Crypto',
  other: 'Other',
};

const typeOptions = computed(() => [...new Set(accounts.items.map(({ type }) => type))].sort());
const currencyOptions = computed(() =>
  [...new Set(accounts.items.map(({ currencyCode }) => currencyCode))].sort(),
);
const visibleAccounts = computed(() =>
  accounts.items.filter(
    (account) =>
      (typeFilter.value === 'all' || account.type === typeFilter.value) &&
      (currencyFilter.value === 'all' || account.currencyCode === currencyFilter.value),
  ),
);

function mapError(error: unknown): ApiErrorShape {
  if (error instanceof ApiError) return error.details;
  return {
    status: 0,
    errorCode: 'UNKNOWN_ERROR',
    message: 'Accounts could not be loaded.',
    traceId: '',
    fieldErrors: {},
    retryable: true,
  };
}

async function load(nextStatus: AccountListStatus = status.value): Promise<void> {
  status.value = nextStatus;
  typeFilter.value = 'all';
  currencyFilter.value = 'all';
  pageError.value = null;
  try {
    await accounts.loadList(nextStatus);
  } catch (error) {
    pageError.value = mapError(error);
  }
}

function openCreate(): void {
  createError.value = '';
  createFieldErrors.value = {};
  createOpen.value = true;
}

async function submitCreate(value: AccountFormValue): Promise<void> {
  createPending.value = true;
  createError.value = '';
  createFieldErrors.value = {};
  try {
    const account = await accounts.create(value);
    createOpen.value = false;
    await router.push({ name: 'account-detail', params: { accountId: account.id } });
  } catch (error) {
    const mapped = mapError(error);
    createError.value = mapped.message;
    createFieldErrors.value = mapped.fieldErrors;
  } finally {
    createPending.value = false;
  }
}

onMounted(() => load());
</script>

<template>
  <AppShell>
    <div class="content-header">
      <div>
        <p class="eyebrow">Ledger / Accounts</p>
        <h1>Accounts</h1>
        <p class="content-header__copy">Places where money is held, invested, or owed.</p>
      </div>
      <button class="button" type="button" @click="openCreate">
        <Plus :size="17" /> New account
      </button>
    </div>

    <div v-if="pageError" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>Accounts unavailable</strong>
        <p>{{ pageError.message }}</p>
        <small v-if="pageError.traceId">Trace {{ pageError.traceId }}</small>
      </div>
      <button
        v-if="pageError.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load()"
      >
        <RefreshCw :size="15" /> Try again
      </button>
    </div>

    <div class="account-toolbar">
      <div class="segmented-control" aria-label="Account status">
        <button
          type="button"
          :aria-pressed="status === 'active'"
          :class="{ 'is-active': status === 'active' }"
          @click="load('active')"
        >
          Active
        </button>
        <button
          type="button"
          :aria-pressed="status === 'archived'"
          :class="{ 'is-active': status === 'archived' }"
          @click="load('archived')"
        >
          Archived
        </button>
      </div>
      <div class="account-filters">
        <Filter :size="16" aria-hidden="true" />
        <label>
          <span class="sr-only">Filter by account type</span>
          <select v-model="typeFilter" aria-label="Filter by account type">
            <option value="all">All types</option>
            <option v-for="type in typeOptions" :key="type" :value="type">
              {{ accountTypeLabels[type] ?? type }}
            </option>
          </select>
        </label>
        <label>
          <span class="sr-only">Filter by currency</span>
          <select v-model="currencyFilter" aria-label="Filter by currency">
            <option value="all">All currencies</option>
            <option v-for="currency in currencyOptions" :key="currency" :value="currency">
              {{ currency }}
            </option>
          </select>
        </label>
      </div>
    </div>

    <section class="account-list" aria-live="polite" :aria-busy="accounts.listState === 'loading'">
      <div class="account-list__head" aria-hidden="true">
        <span>Account</span><span>Type</span><span>Currency</span><span>Class</span
        ><span>Status</span>
      </div>
      <div v-if="accounts.listState === 'loading'" class="account-list__loading">
        <span v-for="index in 4" :key="index"></span>
      </div>
      <template v-else-if="visibleAccounts.length">
        <RouterLink
          v-for="account in visibleAccounts"
          :key="account.id"
          class="account-list__row"
          :to="{ name: 'account-detail', params: { accountId: account.id } }"
        >
          <span class="account-list__identity">
            <span class="account-symbol">
              <Landmark v-if="account.type === 'savings'" :size="18" />
              <WalletCards v-else :size="18" />
            </span>
            <span
              ><strong>{{ account.name }}</strong
              ><small>{{ account.subtype }}</small></span
            >
          </span>
          <span data-label="Type">{{ accountTypeLabels[account.type] ?? account.type }}</span>
          <span data-label="Currency" class="mono-value">{{ account.currencyCode }}</span>
          <span data-label="Class">{{ account.category === 'asset' ? 'Asset' : 'Liability' }}</span>
          <span data-label="Status">
            <span v-if="account.isArchived" class="status-badge status-badge--archived">
              <Archive :size="13" /> Archived
            </span>
            <span v-else class="status-badge">Active</span>
          </span>
        </RouterLink>
      </template>
      <div v-else class="account-empty">
        <WalletCards :size="28" />
        <strong>{{
          accounts.items.length ? 'No accounts match these filters.' : `No ${status} accounts.`
        }}</strong>
        <button
          v-if="status === 'active' && !accounts.items.length"
          class="button button--small"
          type="button"
          @click="openCreate"
        >
          <Plus :size="15" /> Create account
        </button>
      </div>
    </section>

    <AccountFormDialog
      :open="createOpen"
      mode="create"
      :currencies="userContext.currencies"
      :pending="createPending"
      :error="createError"
      :field-errors="createFieldErrors"
      @close="createOpen = false"
      @submit="submitCreate"
    />
  </AppShell>
</template>
