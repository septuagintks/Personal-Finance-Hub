<script setup lang="ts">
import { onMounted, reactive, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { ArrowRightLeft, CircleAlert, Filter, LoaderCircle, Plus, RotateCcw } from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import TransferFormDialog from '../components/TransferFormDialog.vue';
import { ApiError, type ApiErrorShape } from '../services/api-error';
import { formatDecimalString, formatInstant } from '../services/presentation';
import type { CreateTransferRequest, TransferFilters } from '../services/transfer-api';
import { useAccountStore } from '../stores/accounts';
import { useTransferStore } from '../stores/transfers';
import { useUserContextStore } from '../stores/user-context';
import { translate } from '../i18n';
import { naturalDayWindow } from '../services/zoned-date-time';

const route = useRoute();
const router = useRouter();
const accounts = useAccountStore();
const transfers = useTransferStore();
const userContext = useUserContextStore();
const createOpen = ref(false);
const createPending = ref(false);
const createError = ref('');
const pageError = ref<ApiErrorShape | null>(null);
const draft = reactive({ accountId: '', from: '', to: '' });

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
  try {
    const window = naturalDayWindow(value, userContext.preference?.timezone ?? 'UTC');
    return end ? window.end : window.start;
  } catch {
    return undefined;
  }
}

function filtersFromRoute(): TransferFilters {
  return {
    accountId: positiveId(queryString('accountId')),
    from: dateBoundary(queryString('from'), false),
    to: dateBoundary(queryString('to'), true),
    pageSize: 50,
  };
}

function syncDraft(): void {
  draft.accountId = queryString('accountId');
  draft.from = queryString('from');
  draft.to = queryString('to');
}

function mapError(error: unknown): ApiErrorShape {
  if (error instanceof ApiError) return error.details;
  return {
    status: 0,
    errorCode: 'UNKNOWN_ERROR',
    message: translate('transfers.loadFailed'),
    traceId: '',
    fieldErrors: {},
    retryable: true,
  };
}

async function load(): Promise<void> {
  pageError.value = null;
  try {
    await transfers.load(filtersFromRoute());
  } catch (error) {
    pageError.value = mapError(error);
  }
}

async function applyFilters(): Promise<void> {
  const query = Object.fromEntries(Object.entries(draft).filter(([, value]) => value));
  await router.replace({ name: 'transfers', query });
}

async function clearFilters(): Promise<void> {
  await router.replace({ name: 'transfers' });
}

function accountName(accountId: number): string {
  return (
    accounts.items.find(({ id }) => id === accountId)?.name ??
    translate('ledger.accountNumber', { id: accountId })
  );
}

function amount(value: string): string {
  return formatDecimalString(
    value,
    userContext.preference?.locale ?? 'en-US',
    8,
    userContext.preference?.numberFormat,
  );
}

function occurred(value: string): string {
  return formatInstant(value, {
    locale: userContext.preference?.locale ?? 'en-US',
    timeZone: userContext.preference?.timezone ?? 'UTC',
    dateFormat: userContext.preference?.dateFormat,
  });
}

async function submitCreate(payload: CreateTransferRequest): Promise<void> {
  createPending.value = true;
  createError.value = '';
  try {
    const result = await transfers.create(payload);
    createOpen.value = false;
    await router.push({
      name: 'transfer-detail',
      params: { transferGroupId: result.transferGroupId },
    });
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
  await accounts.loadList('active').catch(() => undefined);
  await load();
});
</script>

<template>
  <AppShell>
    <div class="content-header">
      <div>
        <p class="eyebrow">{{ translate('transfers.eyebrow') }}</p>
        <h1>{{ translate('transfers.title') }}</h1>
        <p class="content-header__copy">{{ translate('transfers.description') }}</p>
      </div>
      <button
        class="button"
        type="button"
        :disabled="accounts.items.length < 2"
        @click="createOpen = true"
      >
        <Plus :size="17" /> {{ translate('transfers.record') }}
      </button>
    </div>

    <form
      class="ledger-filters transfer-filters"
      :aria-label="translate('transfers.filters')"
      @submit.prevent="applyFilters"
    >
      <Filter :size="17" aria-hidden="true" />
      <label>
        <span>{{ translate('ledger.account') }}</span>
        <select v-model="draft.accountId">
          <option value="">{{ translate('ledger.allAccounts') }}</option>
          <option v-for="account in accounts.items" :key="account.id" :value="String(account.id)">
            {{ account.name }}
          </option>
        </select>
      </label>
      <label
        ><span>{{ translate('ledger.from') }}</span
        ><input v-model="draft.from" type="date"
      /></label>
      <label
        ><span>{{ translate('ledger.to') }}</span
        ><input v-model="draft.to" type="date"
      /></label>
      <div class="ledger-filters__actions">
        <button
          class="icon-button"
          type="button"
          :title="translate('ledger.clearFilters')"
          :aria-label="translate('ledger.clearFilters')"
          @click="clearFilters"
        >
          <RotateCcw :size="17" />
        </button>
        <button class="button button--small" type="submit">{{ translate('ledger.apply') }}</button>
      </div>
    </form>

    <div v-if="pageError" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('transfers.unavailable') }}</strong>
        <p>{{ pageError.message }}</p>
      </div>
      <button
        v-if="pageError.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load"
      >
        {{ translate('ledger.tryAgain') }}
      </button>
    </div>

    <section
      class="transfer-list"
      aria-live="polite"
      :aria-busy="transfers.listState === 'loading'"
    >
      <div class="transfer-list__head" aria-hidden="true">
        <span>{{ translate('ledger.when') }}</span
        ><span>{{ translate('transfers.route') }}</span
        ><span>{{ translate('transfers.sent') }}</span
        ><span>{{ translate('transfers.received') }}</span
        ><span>{{ translate('transfers.fee') }}</span>
      </div>
      <div v-if="transfers.listState === 'loading'" class="account-list__loading">
        <span v-for="index in 5" :key="index"></span>
      </div>
      <template v-else-if="transfers.items.length">
        <RouterLink
          v-for="transfer in transfers.items"
          :key="transfer.transferGroupId"
          class="transfer-list__row"
          :to="{ name: 'transfer-detail', params: { transferGroupId: transfer.transferGroupId } }"
        >
          <time :datetime="transfer.occurredAt">{{ occurred(transfer.occurredAt) }}</time>
          <span class="transfer-route">
            <strong>{{ accountName(transfer.sourceAccountId) }}</strong>
            <ArrowRightLeft :size="15" />
            <strong>{{ accountName(transfer.targetAccountId) }}</strong>
            <small>{{
              transfer.description ||
              translate('transfers.number', { id: transfer.transferGroupId })
            }}</small>
          </span>
          <strong
            >{{ amount(transfer.outgoingAmount) }}
            <small>{{ transfer.sourceCurrencyCode }}</small></strong
          >
          <strong
            >{{ amount(transfer.incomingAmount) }}
            <small>{{ transfer.targetCurrencyCode }}</small></strong
          >
          <span>{{
            transfer.feeAmount ? `${amount(transfer.feeAmount)} ${transfer.feeCurrencyCode}` : '—'
          }}</span>
        </RouterLink>
      </template>
      <div v-else class="account-empty">
        <ArrowRightLeft :size="28" /><strong>{{ translate('transfers.noMatch') }}</strong>
      </div>
    </section>
    <div v-if="transfers.nextCursor" class="ledger-load-more">
      <button
        class="button button--quiet"
        type="button"
        :disabled="transfers.listState === 'loading-more'"
        @click="transfers.loadMore()"
      >
        <LoaderCircle v-if="transfers.listState === 'loading-more'" class="spin" :size="17" />
        {{ translate('ledger.loadMore') }}
      </button>
    </div>

    <TransferFormDialog
      :open="createOpen"
      mode="create"
      :accounts="accounts.items"
      :time-zone="userContext.preference?.timezone ?? 'UTC'"
      :pending="createPending"
      :error="createError"
      @close="createOpen = false"
      @submit="submitCreate"
    />
  </AppShell>
</template>
