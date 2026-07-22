<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { ArrowUpToLine, CircleAlert, DatabaseZap, Filter, RefreshCw, RotateCcw } from '@lucide/vue';
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
import { createResidentPageWindow } from '../services/resident-page-window';
import { localDateTimeToInstant } from '../services/zoned-date-time';
import { useAccountStore } from '../stores/accounts';
import { useUserContextStore } from '../stores/user-context';
import { translate, type MessageKey } from '../i18n';

const accounts = useAccountStore();
const userContext = useUserContextStore();
const auditItems = ref<UserAuditLogItem[]>([]);
const hasEvictedAuditItems = ref(false);
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
const auditWindow = createResidentPageWindow<UserAuditLogItem>(({ id }) => id);

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
const actionLabels: Record<UserAuditAction, MessageKey> = {
  create: 'maintenance.action.create',
  update: 'maintenance.action.update',
  archive: 'maintenance.action.archive',
  delete: 'maintenance.action.delete',
  dangerous_delete: 'maintenance.action.dangerousDelete',
  sync_import: 'maintenance.action.syncImport',
  refresh: 'maintenance.action.refresh',
  register: 'maintenance.action.register',
  login: 'maintenance.action.login',
  logout: 'maintenance.action.logout',
  token_refresh: 'maintenance.action.tokenRefresh',
  security_event: 'maintenance.action.securityEvent',
};

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
  return localDateTimeToInstant(value, userContext.preference?.timezone ?? 'UTC');
}

async function loadAudit(reset = true): Promise<void> {
  if (!reset && !nextCursor.value) return;
  let from: string | undefined;
  let to: string | undefined;
  try {
    from = isoInput(fromInput.value);
    to = isoInput(toInput.value);
  } catch {
    showError(translate('ledger.invalidLocalTime'), translate('ledger.invalidLocalTime'));
    return;
  }
  const cursor = reset ? undefined : (nextCursor.value ?? undefined);
  const generation = ++auditGeneration;
  auditController?.abort();
  const controller = new AbortController();
  auditController = controller;
  auditLoading.value = true;
  if (reset) {
    auditWindow.clear();
    auditItems.value = [];
    hasEvictedAuditItems.value = false;
    nextCursor.value = null;
  }
  clearError();
  try {
    const page = await listUserAuditLogs(
      {
        action: action.value || undefined,
        resourceType: resourceType.value.trim() || undefined,
        from,
        to,
        pageSize: 50,
      },
      cursor,
      controller.signal,
    );
    if (generation !== auditGeneration || controller.signal.aborted) return;
    const update = reset ? auditWindow.reset(page.items) : auditWindow.append(page.items);
    auditItems.value = update.items;
    hasEvictedAuditItems.value = hasEvictedAuditItems.value || update.evicted;
    nextCursor.value = page.nextCursor;
  } catch (reason) {
    if (generation === auditGeneration && !controller.signal.aborted) {
      showError(reason, translate('maintenance.auditLoadFailed'));
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
    if (!controller.signal.aborted) showError(reason, translate('maintenance.rebuildFailed'));
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
    showError(accountFailure.reason, translate('maintenance.accountsLoadFailed'));
  }
});

onBeforeUnmount(() => {
  auditGeneration += 1;
  auditController?.abort();
  rebuildController?.abort();
  auditWindow.clear();
});
</script>

<template>
  <AppShell>
    <div class="content-header maintenance-header">
      <div>
        <p class="eyebrow">{{ translate('maintenance.eyebrow') }}</p>
        <h1>{{ translate('maintenance.title') }}</h1>
      </div>
      <button
        class="button button--quiet"
        type="button"
        :disabled="auditLoading"
        @click="loadAudit()"
      >
        <RefreshCw :size="16" :class="{ spin: auditLoading }" />
        {{ translate('common.refresh') }}
      </button>
    </div>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('maintenance.requestFailed') }}</strong>
        <p>{{ error }}</p>
        <small v-if="errorTraceId">{{
          translate('maintenance.traceValue', { traceId: errorTraceId })
        }}</small>
      </div>
    </div>

    <section class="maintenance-tool" aria-labelledby="balance-cache-heading">
      <div class="section-heading">
        <div>
          <p class="section-kicker">{{ translate('maintenance.balances') }}</p>
          <h2 id="balance-cache-heading">{{ translate('maintenance.cacheRebuild') }}</h2>
        </div>
        <DatabaseZap :size="22" aria-hidden="true" />
      </div>
      <div class="maintenance-actions">
        <label class="field">
          <span>{{ translate('ledger.account') }}</span>
          <select
            v-model="selectedAccountId"
            :disabled="rebuilding || accounts.listState === 'loading'"
          >
            <option value="">{{ translate('maintenance.selectAccount') }}</option>
            <option v-for="account in accounts.items" :key="account.id" :value="String(account.id)">
              {{ account.name }} · {{ account.currencyCode
              }}{{ account.isArchived ? ` · ${translate('accounts.archived')}` : '' }}
            </option>
          </select>
        </label>
        <button
          class="button button--quiet"
          type="button"
          :disabled="rebuilding || !selectedAccountId"
          @click="rebuild('selected')"
        >
          <RotateCcw :size="16" /> {{ translate('maintenance.rebuildSelected') }}
        </button>
        <button class="button" type="button" :disabled="rebuilding" @click="rebuild('all')">
          <DatabaseZap :size="16" /> {{ translate('maintenance.rebuildAll') }}
        </button>
      </div>

      <div v-if="rebuildItems.length" class="rebuild-result" aria-live="polite">
        <div v-for="item in rebuildItems" :key="item.accountId" class="rebuild-result__row">
          <strong>{{ translate('ledger.accountNumber', { id: item.accountId }) }}</strong>
          <span
            >{{
              formatDecimalString(item.balance, locale, 8, userContext.preference?.numberFormat)
            }}
            {{ item.currencyCode }}</span
          >
          <span>{{
            translate('maintenance.cacheVersions', {
              source: item.sourceVersion,
              cache: item.cacheVersion,
            })
          }}</span>
          <time :datetime="item.rebuiltAt">{{ displayTime(item.rebuiltAt) }}</time>
        </div>
      </div>
    </section>

    <section class="maintenance-audit" aria-labelledby="audit-heading">
      <div class="section-heading">
        <div>
          <p class="section-kicker">{{ translate('maintenance.securityRecord') }}</p>
          <h2 id="audit-heading">{{ translate('maintenance.auditActivity') }}</h2>
        </div>
      </div>

      <form
        class="audit-filters"
        :aria-label="translate('maintenance.auditFilters')"
        @submit.prevent="loadAudit()"
      >
        <Filter :size="18" aria-hidden="true" />
        <label class="field">
          <span>{{ translate('maintenance.action') }}</span>
          <select v-model="action">
            <option value="">{{ translate('maintenance.allActions') }}</option>
            <option v-for="item in actionOptions" :key="item" :value="item">
              {{ translate(actionLabels[item]) }}
            </option>
          </select>
        </label>
        <label class="field">
          <span>{{ translate('maintenance.resource') }}</span>
          <input
            v-model="resourceType"
            maxlength="64"
            :placeholder="translate('maintenance.resourcePlaceholder')"
          />
        </label>
        <label class="field">
          <span>{{ translate('ledger.from') }}</span>
          <input v-model="fromInput" type="datetime-local" :max="toInput || undefined" />
        </label>
        <label class="field">
          <span>{{ translate('ledger.to') }}</span>
          <input v-model="toInput" type="datetime-local" :min="fromInput || undefined" />
        </label>
        <div class="audit-filters__actions">
          <button class="button button--quiet button--small" type="button" @click="resetFilters">
            {{ translate('ledger.clearFilters') }}
          </button>
          <button class="button button--small" type="submit" :disabled="auditLoading">
            {{ translate('ledger.apply') }}
          </button>
        </div>
      </form>

      <div class="audit-list" :aria-busy="auditLoading" tabindex="0">
        <div class="audit-list__head" aria-hidden="true">
          <span>{{ translate('ledger.when') }}</span
          ><span>{{ translate('maintenance.action') }}</span
          ><span>{{ translate('maintenance.resource') }}</span
          ><span>{{ translate('maintenance.result') }}</span
          ><span>{{ translate('maintenance.trace') }}</span>
        </div>
        <div v-for="item in auditItems" :key="item.id" class="audit-list__row">
          <time :datetime="item.occurredAt">{{ displayTime(item.occurredAt) }}</time>
          <strong>{{ translate(actionLabels[item.action]) }}</strong>
          <span>{{ item.resourceType }} · {{ item.resourceId }}</span>
          <span class="status-badge">{{ translate('maintenance.success') }}</span>
          <code>{{ item.traceId ?? translate('maintenance.unavailable') }}</code>
        </div>
        <div v-if="!auditLoading && !auditItems.length" class="empty-state">
          <p>{{ translate('maintenance.noActivity') }}</p>
          <span>{{ translate('maintenance.noActivityDetail') }}</span>
        </div>
      </div>
      <div v-if="hasEvictedAuditItems" class="resident-window-notice" role="status">
        <span>{{ translate('ledger.residentWindowLimited') }}</span>
        <button class="button button--small button--quiet" type="button" @click="loadAudit()">
          <ArrowUpToLine :size="16" /> {{ translate('ledger.returnToLatest') }}
        </button>
      </div>
      <div v-if="nextCursor" class="ledger-load-more">
        <button
          class="button button--quiet"
          type="button"
          :disabled="auditLoading"
          @click="loadAudit(false)"
        >
          {{ translate('ledger.loadMore') }}
        </button>
      </div>
    </section>
  </AppShell>
</template>
