<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { RouterLink, useRoute, useRouter } from 'vue-router';
import {
  Archive,
  ArrowLeft,
  CircleAlert,
  Edit3,
  LoaderCircle,
  RefreshCw,
  RotateCcw,
  Trash2,
  WalletCards,
} from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import AccountFormDialog, { type AccountFormValue } from '../components/AccountFormDialog.vue';
import ConfirmDialog from '../components/ConfirmDialog.vue';
import DeleteAccountDialog from '../components/DeleteAccountDialog.vue';
import { ApiError, type ApiErrorShape } from '../services/api-error';
import { formatDecimalString, formatInstant } from '../services/presentation';
import { useAccountStore } from '../stores/accounts';
import { useUserContextStore } from '../stores/user-context';
import { translate, type MessageKey } from '../i18n';

const route = useRoute();
const router = useRouter();
const accounts = useAccountStore();
const userContext = useUserContextStore();
const pageError = ref<ApiErrorShape | null>(null);
const actionError = ref<ApiErrorShape | null>(null);
const actionPending = ref(false);
const editOpen = ref(false);
const editError = ref('');
const editFieldErrors = ref<Record<string, string>>({});
const confirmation = ref<'archive' | 'restore' | null>(null);
const deleteOpen = ref(false);

const accountId = computed(() => {
  const value = String(route.params.accountId ?? '');
  if (!/^[1-9][0-9]*$/.test(value)) return Number.NaN;
  return Number(value);
});
const account = computed(() => accounts.selected);
const balance = computed(() => accounts.selectedBalance);

const accountTypeLabels: Record<string, MessageKey> = {
  cash: 'accountForm.cash',
  savings: 'accountForm.savings',
  credit: 'accountForm.credit',
  digital_wallet: 'accountForm.digitalWallet',
  investment: 'accountForm.investment',
  crypto: 'accountForm.crypto',
  other: 'accountForm.other',
};

function mapError(error: unknown, fallback: string): ApiErrorShape {
  if (error instanceof ApiError) return error.details;
  return {
    status: 0,
    errorCode: 'UNKNOWN_ERROR',
    message: fallback,
    traceId: '',
    fieldErrors: {},
    retryable: true,
  };
}

function formatDate(value: string | null | undefined): string {
  if (!value) return '—';
  return formatInstant(value, {
    locale: userContext.preference?.locale ?? 'en-US',
    timeZone: userContext.preference?.timezone ?? 'UTC',
    dateFormat: userContext.preference?.dateFormat,
  });
}

function formatAmount(value: string): string {
  return formatDecimalString(
    value,
    userContext.preference?.locale ?? 'en-US',
    8,
    userContext.preference?.numberFormat,
  );
}

async function load(): Promise<void> {
  pageError.value = null;
  if (!Number.isSafeInteger(accountId.value) || accountId.value <= 0) {
    pageError.value = mapError(null, translate('accountDetail.invalidId'));
    return;
  }
  try {
    await accounts.loadDetail(accountId.value);
  } catch (error) {
    pageError.value = mapError(error, translate('accountDetail.loadFailed'));
  }
}

function openEdit(): void {
  editError.value = '';
  editFieldErrors.value = {};
  actionError.value = null;
  editOpen.value = true;
}

async function submitEdit(value: AccountFormValue): Promise<void> {
  actionPending.value = true;
  editError.value = '';
  editFieldErrors.value = {};
  try {
    if (!value.category) throw new Error(translate('accountDetail.classificationRequired'));
    await accounts.updateSelected({ ...value, category: value.category });
    editOpen.value = false;
  } catch (error) {
    const mapped = mapError(error, translate('accountDetail.updateFailed'));
    editError.value =
      mapped.status === 409 ? translate('accountDetail.changedReload') : mapped.message;
    editFieldErrors.value = mapped.fieldErrors;
  } finally {
    actionPending.value = false;
  }
}

async function applyLifecycle(): Promise<void> {
  const operation = confirmation.value;
  if (!operation) return;
  actionPending.value = true;
  actionError.value = null;
  try {
    if (operation === 'archive') {
      await accounts.archiveSelected();
    } else {
      await accounts.restoreSelected();
    }
    confirmation.value = null;
  } catch (error) {
    const mapped = mapError(error, translate('accountDetail.stateFailed'));
    actionError.value =
      mapped.status === 409
        ? { ...mapped, message: translate('accountDetail.changedRetry') }
        : mapped;
    confirmation.value = null;
  } finally {
    actionPending.value = false;
  }
}

async function permanentlyDelete(): Promise<void> {
  actionPending.value = true;
  actionError.value = null;
  try {
    await accounts.permanentlyDeleteSelected();
    deleteOpen.value = false;
    await router.replace({ name: 'accounts' });
  } catch (error) {
    actionError.value = mapError(error, translate('accountDetail.deleteFailed'));
  } finally {
    actionPending.value = false;
  }
}

watch(accountId, load);
onMounted(load);
</script>

<template>
  <AppShell>
    <RouterLink class="back-link account-back" :to="{ name: 'accounts' }">
      <ArrowLeft :size="16" /> {{ translate('accounts.title') }}
    </RouterLink>

    <div v-if="pageError" class="page-alert account-page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('accountDetail.unavailable') }}</strong>
        <p>{{ pageError.message }}</p>
        <small v-if="pageError.traceId">{{
          translate('accounts.trace', { traceId: pageError.traceId })
        }}</small>
      </div>
      <button
        v-if="pageError.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load"
      >
        <RefreshCw :size="15" /> {{ translate('accounts.tryAgain') }}
      </button>
    </div>

    <div v-else-if="accounts.detailState === 'loading'" class="account-detail-loading">
      <LoaderCircle class="spin" :size="24" />
      <span>{{ translate('accountDetail.loading') }}</span>
    </div>

    <template v-else-if="account">
      <div class="content-header account-detail-header">
        <div>
          <div class="account-title-line">
            <span class="account-symbol account-symbol--large"><WalletCards :size="22" /></span>
            <div>
              <p class="eyebrow">
                {{ translate('accountDetail.eyebrow', { currency: account.currencyCode }) }}
              </p>
              <h1>{{ account.name }}</h1>
            </div>
          </div>
          <p class="content-header__copy">{{ account.subtype }}</p>
        </div>
        <div class="account-header-actions">
          <button class="button button--quiet" type="button" @click="openEdit">
            <Edit3 :size="16" /> {{ translate('accountDetail.edit') }}
          </button>
          <button
            v-if="account.isArchived"
            class="button"
            type="button"
            @click="confirmation = 'restore'"
          >
            <RotateCcw :size="16" /> {{ translate('accountDetail.restore') }}
          </button>
          <button
            v-else
            class="button button--quiet"
            type="button"
            @click="confirmation = 'archive'"
          >
            <Archive :size="16" /> {{ translate('accountDetail.archive') }}
          </button>
        </div>
      </div>

      <div v-if="actionError" class="page-alert" role="alert">
        <CircleAlert :size="19" />
        <div>
          <strong>{{ translate('accountDetail.actionNotCompleted') }}</strong>
          <p>{{ actionError.message }}</p>
          <small v-if="actionError.traceId">{{
            translate('accounts.trace', { traceId: actionError.traceId })
          }}</small>
        </div>
        <button
          v-if="actionError.status === 409 || actionError.retryable"
          class="button button--small button--quiet"
          type="button"
          @click="load"
        >
          <RefreshCw :size="15" /> {{ translate('accountDetail.reload') }}
        </button>
      </div>

      <div v-if="account.isArchived" class="account-archive-notice">
        <Archive :size="18" />
        <span>{{ translate('accountDetail.archivedNotice') }}</span>
      </div>

      <section class="account-balance-band" :aria-label="translate('accountDetail.balanceLabel')">
        <div>
          <p>{{ translate('accountDetail.currentBalance') }}</p>
          <strong>{{ balance ? formatAmount(balance.balance) : '—' }}</strong>
          <span>{{ balance?.currencyCode ?? account.currencyCode }}</span>
        </div>
        <dl>
          <div>
            <dt>{{ translate('accountDetail.balanceUpdated') }}</dt>
            <dd>{{ formatDate(balance?.updatedAt) }}</dd>
          </div>
          <div>
            <dt>{{ translate('accountDetail.lastTransaction') }}</dt>
            <dd>{{ balance?.lastTransactionId ?? translate('accountDetail.none') }}</dd>
          </div>
        </dl>
      </section>

      <section class="account-detail-section">
        <div class="section-heading">
          <p class="section-kicker">{{ translate('accountDetail.profile') }}</p>
          <h2>{{ translate('accountDetail.details') }}</h2>
        </div>
        <dl class="account-detail-grid">
          <div>
            <dt>{{ translate('accounts.type') }}</dt>
            <dd>
              {{
                accountTypeLabels[account.type]
                  ? translate(accountTypeLabels[account.type])
                  : account.type
              }}
            </dd>
          </div>
          <div>
            <dt>{{ translate('accountDetail.classification') }}</dt>
            <dd>
              {{
                translate(
                  account.category === 'asset' ? 'accountForm.asset' : 'accountForm.liability',
                )
              }}
            </dd>
          </div>
          <div>
            <dt>{{ translate('accounts.currency') }}</dt>
            <dd class="mono-value">{{ account.currencyCode }}</dd>
          </div>
          <div>
            <dt>{{ translate('accounts.status') }}</dt>
            <dd>{{ translate(account.isArchived ? 'accounts.archived' : 'accounts.active') }}</dd>
          </div>
          <div>
            <dt>{{ translate('accountDetail.created') }}</dt>
            <dd>{{ formatDate(account.createdAt) }}</dd>
          </div>
          <div>
            <dt>{{ translate('accountDetail.lastChanged') }}</dt>
            <dd>{{ formatDate(account.updatedAt) }}</dd>
          </div>
          <div class="account-detail-grid__wide">
            <dt>{{ translate('accountDetail.description') }}</dt>
            <dd>{{ account.description || '—' }}</dd>
          </div>
        </dl>
      </section>

      <section class="danger-zone">
        <div>
          <p class="section-kicker">{{ translate('accountDetail.dangerZone') }}</p>
          <h2>{{ translate('accountDetail.permanentRemoval') }}</h2>
          <p>{{ translate('accountDetail.permanentRemovalDescription') }}</p>
        </div>
        <button class="button button--danger-quiet" type="button" @click="deleteOpen = true">
          <Trash2 :size="16" /> {{ translate('accountDetail.delete') }}
        </button>
      </section>

      <AccountFormDialog
        :open="editOpen"
        mode="edit"
        :account="account"
        :currencies="userContext.currencies"
        :default-currency="userContext.preference?.baseCurrency"
        :pending="actionPending"
        :error="editError"
        :field-errors="editFieldErrors"
        @close="editOpen = false"
        @submit="submitEdit"
      />

      <ConfirmDialog
        :open="confirmation !== null"
        :title="
          translate(
            confirmation === 'archive'
              ? 'accountDetail.archiveTitle'
              : 'accountDetail.restoreTitle',
          )
        "
        :description="
          confirmation === 'archive'
            ? translate('accountDetail.archiveDescription')
            : translate('accountDetail.restoreDescription')
        "
        :confirm-label="
          translate(
            confirmation === 'archive'
              ? 'accountDetail.archiveTitle'
              : 'accountDetail.restoreTitle',
          )
        "
        :action="confirmation === 'restore' ? 'restore' : 'archive'"
        :pending="actionPending"
        @close="confirmation = null"
        @confirm="applyLifecycle"
      />

      <DeleteAccountDialog
        :open="deleteOpen"
        :account="account"
        :pending="actionPending"
        :error="deleteOpen ? (actionError?.message ?? '') : ''"
        @close="deleteOpen = false"
        @confirm="permanentlyDelete"
      />
    </template>
  </AppShell>
</template>
