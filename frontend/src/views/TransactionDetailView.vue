<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import {
  ArrowLeft,
  CircleAlert,
  FilePenLine,
  LoaderCircle,
  ReceiptText,
  Trash2,
} from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import ModalDialog from '../components/ModalDialog.vue';
import TransactionFormDialog from '../components/TransactionFormDialog.vue';
import { ApiError } from '../services/api-error';
import { formatDecimalString, formatInstant } from '../services/presentation';
import type { CreateTransactionRequest } from '../services/transaction-api';
import { useAccountStore } from '../stores/accounts';
import { useMetadataStore } from '../stores/metadata';
import { useTransactionStore } from '../stores/transactions';
import { useUserContextStore } from '../stores/user-context';
import { translate, type MessageKey } from '../i18n';

const route = useRoute();
const router = useRouter();
const accounts = useAccountStore();
const metadata = useMetadataStore();
const transactions = useTransactionStore();
const userContext = useUserContextStore();
const error = ref('');
const correctionOpen = ref(false);
const correctionPending = ref(false);
const correctionError = ref('');
const deleteOpen = ref(false);
const deletePending = ref(false);

const typeLabels: Record<string, MessageKey> = {
  income: 'ledger.type.income',
  expense: 'ledger.type.expense',
  transfer: 'ledger.type.transfer',
  adjustment: 'ledger.type.adjustment',
};

const transactionId = computed(() => Number(route.params.transactionId));
const transaction = computed(() => transactions.selected);
const canMutate = computed(
  () =>
    transaction.value &&
    !transaction.value.deletedAt &&
    !transaction.value.transferGroupId &&
    transaction.value.type !== 'transfer',
);
const accountName = computed(
  () =>
    accounts.items.find(({ id }) => id === transaction.value?.accountId)?.name ??
    (transaction.value
      ? translate('ledger.accountNumber', { id: transaction.value.accountId })
      : ''),
);

function message(reason: unknown): string {
  return reason instanceof ApiError
    ? reason.details.message
    : translate('transactionDetail.loadFailed');
}

async function load(): Promise<void> {
  error.value = '';
  if (!Number.isSafeInteger(transactionId.value) || transactionId.value <= 0) {
    error.value = translate('transactionDetail.invalidId');
    return;
  }
  try {
    await transactions.loadDetail(transactionId.value);
  } catch (reason) {
    error.value = message(reason);
  }
}

function displayTime(value: string): string {
  return formatInstant(value, {
    locale: userContext.preference?.locale ?? 'en-US',
    timeZone: userContext.preference?.timezone ?? 'UTC',
    dateFormat: userContext.preference?.dateFormat,
  });
}

async function submitCorrection(payload: CreateTransactionRequest): Promise<void> {
  correctionPending.value = true;
  correctionError.value = '';
  try {
    const replacement = await transactions.correctSelected(payload);
    correctionOpen.value = false;
    await router.replace({
      name: 'transaction-detail',
      params: { transactionId: replacement.id },
    });
  } catch (reason) {
    correctionError.value = message(reason);
  } finally {
    correctionPending.value = false;
  }
}

async function remove(): Promise<void> {
  deletePending.value = true;
  try {
    await transactions.deleteSelected();
    await router.push({ name: 'transactions' });
  } catch (reason) {
    error.value = message(reason);
    deleteOpen.value = false;
  } finally {
    deletePending.value = false;
  }
}

watch(transactionId, load);
onMounted(async () => {
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
    <RouterLink class="back-link account-back" :to="{ name: 'transactions' }">
      <ArrowLeft :size="16" /> {{ translate('transactionDetail.back') }}
    </RouterLink>
    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('transactionDetail.unavailable') }}</strong>
        <p>{{ error }}</p>
      </div>
    </div>
    <div v-if="transactions.detailState === 'loading'" class="account-detail-loading">
      <LoaderCircle class="spin" :size="19" /> {{ translate('transactionDetail.loading') }}
    </div>
    <template v-else-if="transaction">
      <div class="content-header transaction-detail-header">
        <div>
          <p class="eyebrow">
            {{ translate('transactionDetail.eyebrow', { id: transaction.id }) }}
          </p>
          <div class="account-title-line">
            <ReceiptText :size="32" />
            <h1>{{ transaction.description || translate(typeLabels[transaction.type]) }}</h1>
          </div>
          <p class="content-header__copy">
            {{ accountName }} · {{ displayTime(transaction.occurredAt) }}
          </p>
        </div>
        <div v-if="canMutate" class="account-header-actions">
          <button class="button button--quiet" type="button" @click="correctionOpen = true">
            <FilePenLine :size="16" /> {{ translate('ledger.correct') }}
          </button>
          <button class="button button--danger" type="button" @click="deleteOpen = true">
            <Trash2 :size="16" /> {{ translate('ledger.delete') }}
          </button>
        </div>
      </div>

      <div v-if="transaction.deletedAt" class="account-archive-notice">
        {{ translate('transactionDetail.inactive') }}
      </div>
      <section class="transaction-amount-band">
        <div>
          <p>{{ translate('ledger.amount') }}</p>
          <strong>{{
            formatDecimalString(
              transaction.amount,
              userContext.preference?.locale ?? 'en-US',
              8,
              userContext.preference?.numberFormat,
            )
          }}</strong
          ><span>{{ transaction.currencyCode }}</span>
        </div>
        <dl>
          <div>
            <dt>{{ translate('ledger.type') }}</dt>
            <dd>{{ translate(typeLabels[transaction.type]) }}</dd>
          </div>
          <div>
            <dt>{{ translate('ledger.account') }}</dt>
            <dd>{{ accountName }}</dd>
          </div>
        </dl>
      </section>

      <section class="account-detail-section">
        <div class="section-heading">
          <p class="section-kicker">{{ translate('transactionDetail.recordedFact') }}</p>
          <h2>{{ translate('transactionDetail.details') }}</h2>
        </div>
        <dl class="account-detail-grid">
          <div>
            <dt>{{ translate('ledger.category') }}</dt>
            <dd>
              {{ transaction.categoryName || translate('ledger.uncategorized')
              }}<small v-if="transaction.categoryDeleted">
                · {{ translate('ledger.archivedSuffix') }}</small
              >
            </dd>
          </div>
          <div>
            <dt>{{ translate('ledger.occurred') }}</dt>
            <dd>{{ displayTime(transaction.occurredAt) }}</dd>
          </div>
          <div>
            <dt>{{ translate('ledger.created') }}</dt>
            <dd>{{ displayTime(transaction.createdAt) }}</dd>
          </div>
          <div class="account-detail-grid__wide">
            <dt>{{ translate('ledger.description') }}</dt>
            <dd>{{ transaction.description || '—' }}</dd>
          </div>
          <div class="account-detail-grid__wide">
            <dt>{{ translate('ledger.tags') }}</dt>
            <dd class="ledger-tags">
              <small v-for="tag in transaction.tags" :key="tag.id"
                >{{ tag.name
                }}<span v-if="tag.isDeleted">
                  · {{ translate('ledger.archivedSuffix') }}</span
                ></small
              ><span v-if="!transaction.tags.length">—</span>
            </dd>
          </div>
        </dl>
      </section>

      <section
        v-if="transaction.correctsTransactionId || transaction.correctedByTransactionId"
        class="correction-links"
      >
        <RouterLink
          v-if="transaction.correctsTransactionId"
          :to="{
            name: 'transaction-detail',
            params: { transactionId: transaction.correctsTransactionId },
          }"
        >
          {{ translate('transactionDetail.original', { id: transaction.correctsTransactionId }) }}
        </RouterLink>
        <RouterLink
          v-if="transaction.correctedByTransactionId"
          :to="{
            name: 'transaction-detail',
            params: { transactionId: transaction.correctedByTransactionId },
          }"
        >
          {{
            translate('transactionDetail.replacement', { id: transaction.correctedByTransactionId })
          }}
        </RouterLink>
      </section>

      <TransactionFormDialog
        :open="correctionOpen"
        mode="correct"
        :transaction="transaction"
        :accounts="accounts.items"
        :categories="metadata.categories"
        :tags="metadata.tags"
        :time-zone="userContext.preference?.timezone ?? 'UTC'"
        :pending="correctionPending"
        :error="correctionError"
        @close="correctionOpen = false"
        @submit="submitCorrection"
      />
      <ModalDialog
        :open="deleteOpen"
        :title="translate('transactionDetail.deleteTitle')"
        @close="deletePending ? undefined : (deleteOpen = false)"
      >
        <p class="dialog-copy">
          {{ translate('transactionDetail.deleteDescription') }}
        </p>
        <footer class="modal-actions">
          <button
            class="button button--quiet"
            type="button"
            :disabled="deletePending"
            @click="deleteOpen = false"
          >
            {{ translate('common.cancel') }}
          </button>
          <button
            class="button button--danger"
            type="button"
            :disabled="deletePending"
            @click="remove"
          >
            <LoaderCircle v-if="deletePending" class="spin" :size="17" /><Trash2
              v-else
              :size="17"
            />
            {{ translate(deletePending ? 'ledger.deleting' : 'transactionDetail.deleteTitle') }}
          </button>
        </footer>
      </ModalDialog>
    </template>
  </AppShell>
</template>
