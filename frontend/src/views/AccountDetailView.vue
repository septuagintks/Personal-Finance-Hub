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
import { formatDecimalString } from '../services/presentation';
import { useAccountStore } from '../stores/accounts';
import { useUserContextStore } from '../stores/user-context';

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

const accountId = computed(() => Number(route.params.accountId));
const account = computed(() => accounts.selected);
const balance = computed(() => accounts.selectedBalance);

const accountTypeLabels: Record<string, string> = {
  cash: 'Cash',
  savings: 'Savings',
  credit: 'Credit',
  digital_wallet: 'Digital wallet',
  investment: 'Investment',
  crypto: 'Crypto',
  other: 'Other',
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
  return new Intl.DateTimeFormat(undefined, {
    year: 'numeric',
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  }).format(new Date(value));
}

async function load(): Promise<void> {
  pageError.value = null;
  if (!Number.isSafeInteger(accountId.value) || accountId.value <= 0) {
    pageError.value = mapError(null, 'This account identifier is invalid.');
    return;
  }
  try {
    await accounts.loadDetail(accountId.value);
  } catch (error) {
    pageError.value = mapError(error, 'Account details could not be loaded.');
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
    await accounts.updateSelected(value);
    editOpen.value = false;
  } catch (error) {
    const mapped = mapError(error, 'The account could not be updated.');
    editError.value =
      mapped.status === 409
        ? 'This account changed elsewhere. Reload it before saving again.'
        : mapped.message;
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
    const mapped = mapError(error, 'The account state could not be changed.');
    actionError.value =
      mapped.status === 409
        ? { ...mapped, message: 'This account changed elsewhere. Reload before trying again.' }
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
    actionError.value = mapError(error, 'The account could not be deleted.');
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
      <ArrowLeft :size="16" /> Accounts
    </RouterLink>

    <div v-if="pageError" class="page-alert account-page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>Account unavailable</strong>
        <p>{{ pageError.message }}</p>
        <small v-if="pageError.traceId">Trace {{ pageError.traceId }}</small>
      </div>
      <button
        v-if="pageError.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load"
      >
        <RefreshCw :size="15" /> Try again
      </button>
    </div>

    <div v-else-if="accounts.detailState === 'loading'" class="account-detail-loading">
      <LoaderCircle class="spin" :size="24" />
      <span>Loading account</span>
    </div>

    <template v-else-if="account">
      <div class="content-header account-detail-header">
        <div>
          <div class="account-title-line">
            <span class="account-symbol account-symbol--large"><WalletCards :size="22" /></span>
            <div>
              <p class="eyebrow">Account / {{ account.currencyCode }}</p>
              <h1>{{ account.name }}</h1>
            </div>
          </div>
          <p class="content-header__copy">{{ account.subtype }}</p>
        </div>
        <div class="account-header-actions">
          <button class="button button--quiet" type="button" @click="openEdit">
            <Edit3 :size="16" /> Edit
          </button>
          <button
            v-if="account.isArchived"
            class="button"
            type="button"
            @click="confirmation = 'restore'"
          >
            <RotateCcw :size="16" /> Restore
          </button>
          <button
            v-else
            class="button button--quiet"
            type="button"
            @click="confirmation = 'archive'"
          >
            <Archive :size="16" /> Archive
          </button>
        </div>
      </div>

      <div v-if="actionError" class="page-alert" role="alert">
        <CircleAlert :size="19" />
        <div>
          <strong>Action not completed</strong>
          <p>{{ actionError.message }}</p>
          <small v-if="actionError.traceId">Trace {{ actionError.traceId }}</small>
        </div>
        <button
          v-if="actionError.status === 409 || actionError.retryable"
          class="button button--small button--quiet"
          type="button"
          @click="load"
        >
          <RefreshCw :size="15" /> Reload
        </button>
      </div>

      <div v-if="account.isArchived" class="account-archive-notice">
        <Archive :size="18" />
        <span
          >Archived accounts remain in history but cannot receive transactions or transfers.</span
        >
      </div>

      <section class="account-balance-band" aria-label="Account balance">
        <div>
          <p>Current balance</p>
          <strong>{{ balance ? formatDecimalString(balance.balance) : '—' }}</strong>
          <span>{{ balance?.currencyCode ?? account.currencyCode }}</span>
        </div>
        <dl>
          <div>
            <dt>Balance updated</dt>
            <dd>{{ formatDate(balance?.updatedAt) }}</dd>
          </div>
          <div>
            <dt>Last transaction</dt>
            <dd>{{ balance?.lastTransactionId ?? 'None' }}</dd>
          </div>
        </dl>
      </section>

      <section class="account-detail-section">
        <div class="section-heading">
          <p class="section-kicker">Profile</p>
          <h2>Account details</h2>
        </div>
        <dl class="account-detail-grid">
          <div>
            <dt>Type</dt>
            <dd>{{ accountTypeLabels[account.type] ?? account.type }}</dd>
          </div>
          <div>
            <dt>Classification</dt>
            <dd>{{ account.category === 'asset' ? 'Asset' : 'Liability' }}</dd>
          </div>
          <div>
            <dt>Currency</dt>
            <dd class="mono-value">{{ account.currencyCode }}</dd>
          </div>
          <div>
            <dt>Status</dt>
            <dd>{{ account.isArchived ? 'Archived' : 'Active' }}</dd>
          </div>
          <div>
            <dt>Created</dt>
            <dd>{{ formatDate(account.createdAt) }}</dd>
          </div>
          <div>
            <dt>Last changed</dt>
            <dd>{{ formatDate(account.updatedAt) }}</dd>
          </div>
          <div class="account-detail-grid__wide">
            <dt>Description</dt>
            <dd>{{ account.description || '—' }}</dd>
          </div>
        </dl>
      </section>

      <section class="danger-zone">
        <div>
          <p class="section-kicker">Danger zone</p>
          <h2>Permanent removal</h2>
          <p>Deleting this account also removes its dependent ledger records.</p>
        </div>
        <button class="button button--danger-quiet" type="button" @click="deleteOpen = true">
          <Trash2 :size="16" /> Delete account
        </button>
      </section>

      <AccountFormDialog
        :open="editOpen"
        mode="edit"
        :account="account"
        :currencies="userContext.currencies"
        :pending="actionPending"
        :error="editError"
        :field-errors="editFieldErrors"
        @close="editOpen = false"
        @submit="submitEdit"
      />

      <ConfirmDialog
        :open="confirmation !== null"
        :title="confirmation === 'archive' ? 'Archive account' : 'Restore account'"
        :description="
          confirmation === 'archive'
            ? 'New transactions and transfers will be blocked while this account is archived.'
            : 'This account will return to active account lists and can be used again.'
        "
        :confirm-label="confirmation === 'archive' ? 'Archive account' : 'Restore account'"
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
