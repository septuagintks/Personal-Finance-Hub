<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { CircleAlert, DatabaseZap, Filter, RefreshCw, RotateCcw } from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import { ApiError } from '../services/api-error';
import {
  listUserAuditLogs,
  rebuildAccountBalanceCache,
  rebuildAllBalanceCaches,
  type BalanceCacheRebuildItem,
  type UserAuditAction,
  type UserAuditLogItem,
} from '../services/maintenance-api';
import { formatDecimalString, formatInstant } from '../services/presentation';
import { useAccountStore } from '../stores/accounts';
import { useUserContextStore } from '../stores/user-context';

const accounts = useAccountStore();
const userContext = useUserContextStore();
const auditItems = ref<UserAuditLogItem[]>([]);
const nextCursor = ref<string | null>(null);
const auditLoading = ref(false);
const rebuilding = ref(false);
const error = ref('');
const errorTraceId = ref('');
const action = ref<UserAuditAction | ''>('');
const resourceType = ref('');
const fromInput = ref('');
const toInput = ref('');
const selectedAccountId = ref('');
const rebuildItems = ref<BalanceCacheRebuildItem[]>([]);
let auditController: AbortController | null = null;
let rebuildController: AbortController | null = null;
let auditGeneration = 0;

const actionOptions: UserAuditAction[] = [
  'create',
  'update',
  'archive',
  'delete',
  'dangerous_delete',
  'sync_import',
  'refresh',
  'register',
  'login',
  'logout',
  'token_refresh',
  'security_event',
];

const locale = computed(() => userContext.preference?.locale ?? 'en-US');
const presentationLocale = computed(() => ({
  locale: locale.value,
  timeZone: userContext.preference?.timezone ?? 'UTC',
  dateFormat: userContext.preference?.dateFormat,
}));

function clearError(): void {
  error.value = '';
  errorTraceId.value = '';
}

function showError(reason: unknown, fallback: string): void {
  if (reason instanceof ApiError) {
    error.value = reason.details.message;
    errorTraceId.value = reason.details.traceId;
    return;
  }
  error.value = fallback;
  errorTraceId.value = '';
}

function isoInput(value: string): string | undefined {
  if (!value) return undefined;
  const parsed = new Date(value);
  return Number.isNaN(parsed.getTime()) ? undefined : parsed.toISOString();
}

async function loadAudit(reset = true): Promise<void> {
  if (!reset && !nextCursor.value) return;
  const cursor = reset ? undefined : (nextCursor.value ?? undefined);
  const generation = ++auditGeneration;
  auditController?.abort();
  const controller = new AbortController();
  auditController = controller;
  auditLoading.value = true;
  if (reset) {
    auditItems.value = [];
    nextCursor.value = null;
  }
  clearError();
  try {
    const page = await listUserAuditLogs(
      {
        action: action.value || undefined,
        resourceType: resourceType.value.trim() || undefined,
        from: isoInput(fromInput.value),
        to: isoInput(toInput.value),
        pageSize: 50,
      },
      cursor,
      controller.signal,
    );
    if (generation !== auditGeneration || controller.signal.aborted) return;
    auditItems.value = reset ? page.items : [...auditItems.value, ...page.items];
    nextCursor.value = page.nextCursor;
  } catch (reason) {
    if (generation === auditGeneration && !controller.signal.aborted) {
      showError(reason, 'Audit activity could not be loaded.');
    }
  } finally {
    if (auditController === controller) {
      auditController = null;
      auditLoading.value = false;
    }
  }
}

async function resetFilters(): Promise<void> {
  action.value = '';
  resourceType.value = '';
  fromInput.value = '';
  toInput.value = '';
  await loadAudit();
}

async function rebuild(scope: 'all' | 'selected'): Promise<void> {
  if (rebuilding.value) return;
  const accountId = Number(selectedAccountId.value);
  if (scope === 'selected' && (!Number.isSafeInteger(accountId) || accountId <= 0)) return;
  rebuildController?.abort();
  const controller = new AbortController();
  rebuildController = controller;
  rebuilding.value = true;
  clearError();
  try {
    const result =
      scope === 'all'
        ? await rebuildAllBalanceCaches(controller.signal)
        : await rebuildAccountBalanceCache(accountId, controller.signal);
    if (controller.signal.aborted) return;
    rebuildItems.value = result.accounts;
    await loadAudit();
  } catch (reason) {
    if (!controller.signal.aborted) showError(reason, 'Balance caches could not be rebuilt.');
  } finally {
    if (rebuildController === controller) {
      rebuildController = null;
      rebuilding.value = false;
    }
  }
}

function displayTime(value: string): string {
  return formatInstant(value, presentationLocale.value);
}

onMounted(async () => {
  const results = await Promise.allSettled([loadAudit(), accounts.loadList('all')]);
  const accountFailure = results[1];
  if (accountFailure?.status === 'rejected' && !error.value) {
    showError(accountFailure.reason, 'Accounts could not be loaded.');
  }
});

onBeforeUnmount(() => {
  auditGeneration += 1;
  auditController?.abort();
  rebuildController?.abort();
});
</script>

<template>
  <AppShell>
    <div class="content-header maintenance-header">
      <div>
        <p class="eyebrow">Account / Activity</p>
        <h1>Maintenance</h1>
      </div>
      <button
        class="button button--quiet"
        type="button"
        :disabled="auditLoading"
        @click="loadAudit()"
      >
        <RefreshCw :size="16" :class="{ spin: auditLoading }" /> Refresh
      </button>
    </div>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>Maintenance request failed</strong>
        <p>{{ error }}</p>
        <small v-if="errorTraceId">Trace {{ errorTraceId }}</small>
      </div>
    </div>

    <section class="maintenance-tool" aria-labelledby="balance-cache-heading">
      <div class="section-heading">
        <div>
          <p class="section-kicker">Balances</p>
          <h2 id="balance-cache-heading">Cache rebuild</h2>
        </div>
        <DatabaseZap :size="22" aria-hidden="true" />
      </div>
      <div class="maintenance-actions">
        <label class="field">
          <span>Account</span>
          <select
            v-model="selectedAccountId"
            :disabled="rebuilding || accounts.listState === 'loading'"
          >
            <option value="">Select account</option>
            <option v-for="account in accounts.items" :key="account.id" :value="String(account.id)">
              {{ account.name }} · {{ account.currencyCode
              }}{{ account.isArchived ? ' · Archived' : '' }}
            </option>
          </select>
        </label>
        <button
          class="button button--quiet"
          type="button"
          :disabled="rebuilding || !selectedAccountId"
          @click="rebuild('selected')"
        >
          <RotateCcw :size="16" /> Rebuild selected
        </button>
        <button class="button" type="button" :disabled="rebuilding" @click="rebuild('all')">
          <DatabaseZap :size="16" /> Rebuild all
        </button>
      </div>

      <div v-if="rebuildItems.length" class="rebuild-result" aria-live="polite">
        <div v-for="item in rebuildItems" :key="item.accountId" class="rebuild-result__row">
          <strong>Account {{ item.accountId }}</strong>
          <span>{{ formatDecimalString(item.balance, locale) }} {{ item.currencyCode }}</span>
          <span>Source {{ item.sourceVersion }} / Cache {{ item.cacheVersion }}</span>
          <time :datetime="item.rebuiltAt">{{ displayTime(item.rebuiltAt) }}</time>
        </div>
      </div>
    </section>

    <section class="maintenance-audit" aria-labelledby="audit-heading">
      <div class="section-heading">
        <div>
          <p class="section-kicker">Security record</p>
          <h2 id="audit-heading">Audit activity</h2>
        </div>
      </div>

      <form class="audit-filters" aria-label="Audit filters" @submit.prevent="loadAudit()">
        <Filter :size="18" aria-hidden="true" />
        <label class="field">
          <span>Action</span>
          <select v-model="action">
            <option value="">All actions</option>
            <option v-for="item in actionOptions" :key="item" :value="item">
              {{ item.replaceAll('_', ' ') }}
            </option>
          </select>
        </label>
        <label class="field">
          <span>Resource</span>
          <input v-model="resourceType" maxlength="64" placeholder="account" />
        </label>
        <label class="field">
          <span>From</span>
          <input v-model="fromInput" type="datetime-local" :max="toInput || undefined" />
        </label>
        <label class="field">
          <span>To</span>
          <input v-model="toInput" type="datetime-local" :min="fromInput || undefined" />
        </label>
        <div class="audit-filters__actions">
          <button class="button button--quiet button--small" type="button" @click="resetFilters">
            Reset
          </button>
          <button class="button button--small" type="submit" :disabled="auditLoading">Apply</button>
        </div>
      </form>

      <div class="audit-list" :aria-busy="auditLoading" tabindex="0">
        <div class="audit-list__head" aria-hidden="true">
          <span>When</span><span>Action</span><span>Resource</span><span>Result</span
          ><span>Trace</span>
        </div>
        <div v-for="item in auditItems" :key="item.id" class="audit-list__row">
          <time :datetime="item.occurredAt">{{ displayTime(item.occurredAt) }}</time>
          <strong>{{ item.action.replaceAll('_', ' ') }}</strong>
          <span>{{ item.resourceType }} · {{ item.resourceId }}</span>
          <span class="status-badge">{{ item.result }}</span>
          <code>{{ item.traceId ?? 'Unavailable' }}</code>
        </div>
        <div v-if="!auditLoading && !auditItems.length" class="empty-state">
          <p>No audit activity found</p>
          <span>Adjust the current filters and try again.</span>
        </div>
      </div>
      <div v-if="nextCursor" class="ledger-load-more">
        <button
          class="button button--quiet"
          type="button"
          :disabled="auditLoading"
          @click="loadAudit(false)"
        >
          Load more
        </button>
      </div>
    </section>
  </AppShell>
</template>
