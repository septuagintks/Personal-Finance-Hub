<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import {
  ArrowLeft,
  ArrowRightLeft,
  CircleAlert,
  FilePenLine,
  LoaderCircle,
  Trash2,
} from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import ModalDialog from '../components/ModalDialog.vue';
import TransferFormDialog from '../components/TransferFormDialog.vue';
import { ApiError } from '../services/api-error';
import { formatDecimalString, formatInstant } from '../services/presentation';
import type { CreateTransferRequest } from '../services/transfer-api';
import { useAccountStore } from '../stores/accounts';
import { useTransferStore } from '../stores/transfers';
import { useUserContextStore } from '../stores/user-context';
import { translate, type MessageKey } from '../i18n';

const route = useRoute();
const router = useRouter();
const accounts = useAccountStore();
const transfers = useTransferStore();
const userContext = useUserContextStore();
const error = ref('');
const correctionOpen = ref(false);
const correctionPending = ref(false);
const correctionError = ref('');
const deleteOpen = ref(false);
const deletePending = ref(false);

const modeLabels: Record<string, MessageKey> = {
  OutgoingAndRate: 'transferForm.sendRate',
  BothAmounts: 'transferForm.bothAmounts',
  IncomingAndRate: 'transferForm.receiveRate',
};

const transferGroupId = computed(() => Number(route.params.transferGroupId));
const transfer = computed(() => transfers.selected);
const canMutate = computed(() => Boolean(transfer.value && !transfer.value.deletedAt));

function accountName(accountId: number | null): string {
  if (!accountId) return '—';
  return (
    accounts.items.find(({ id }) => id === accountId)?.name ??
    translate('ledger.accountNumber', { id: accountId })
  );
}

function message(reason: unknown): string {
  return reason instanceof ApiError
    ? reason.details.message
    : translate('transferDetail.loadFailed');
}

function amount(value: string): string {
  return formatDecimalString(
    value,
    userContext.preference?.locale ?? 'en-US',
    8,
    userContext.preference?.numberFormat,
  );
}

function displayTime(value: string): string {
  return formatInstant(value, {
    locale: userContext.preference?.locale ?? 'en-US',
    timeZone: userContext.preference?.timezone ?? 'UTC',
    dateFormat: userContext.preference?.dateFormat,
  });
}

async function load(): Promise<void> {
  error.value = '';
  if (!Number.isSafeInteger(transferGroupId.value) || transferGroupId.value <= 0) {
    error.value = translate('transferDetail.invalidId');
    return;
  }
  try {
    await transfers.loadDetail(transferGroupId.value);
  } catch (reason) {
    error.value = message(reason);
  }
}

async function submitCorrection(payload: CreateTransferRequest): Promise<void> {
  correctionPending.value = true;
  correctionError.value = '';
  try {
    const replacement = await transfers.correctSelected(payload);
    correctionOpen.value = false;
    await router.replace({
      name: 'transfer-detail',
      params: { transferGroupId: replacement.transferGroupId },
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
    await transfers.deleteSelected();
    await router.push({ name: 'transfers' });
  } catch (reason) {
    error.value = message(reason);
    deleteOpen.value = false;
  } finally {
    deletePending.value = false;
  }
}

watch(transferGroupId, load);
onMounted(async () => {
  await accounts.loadList('all').catch(() => undefined);
  await load();
});
</script>

<template>
  <AppShell>
    <RouterLink class="back-link account-back" :to="{ name: 'transfers' }">
      <ArrowLeft :size="16" /> {{ translate('transferDetail.back') }}
    </RouterLink>
    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('transferDetail.unavailable') }}</strong>
        <p>{{ error }}</p>
      </div>
    </div>
    <div v-if="transfers.detailState === 'loading'" class="account-detail-loading">
      <LoaderCircle class="spin" :size="19" /> {{ translate('transferDetail.loading') }}
    </div>
    <template v-else-if="transfer">
      <div class="content-header transfer-detail-header">
        <div>
          <p class="eyebrow">
            {{ translate('transferDetail.eyebrow', { id: transfer.transferGroupId }) }}
          </p>
          <div class="account-title-line">
            <ArrowRightLeft :size="32" />
            <h1>{{ transfer.description || translate('transferDetail.defaultTitle') }}</h1>
          </div>
          <p class="content-header__copy">{{ displayTime(transfer.occurredAt) }}</p>
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

      <div v-if="transfer.deletedAt" class="account-archive-notice">
        {{ translate('transferDetail.inactive') }}
      </div>
      <section class="transfer-amount-band">
        <div>
          <p>{{ translate('ledger.from') }}</p>
          <strong>{{ amount(transfer.outgoingAmount) }}</strong
          ><span>{{ transfer.sourceCurrencyCode }}</span>
          <small>{{ accountName(transfer.sourceAccountId) }}</small>
        </div>
        <ArrowRightLeft :size="24" aria-hidden="true" />
        <div>
          <p>{{ translate('ledger.to') }}</p>
          <strong>{{ amount(transfer.incomingAmount) }}</strong
          ><span>{{ transfer.targetCurrencyCode }}</span>
          <small>{{ accountName(transfer.targetAccountId) }}</small>
        </div>
      </section>

      <section class="account-detail-section">
        <div class="section-heading">
          <p class="section-kicker">{{ translate('transferDetail.aggregate') }}</p>
          <h2>{{ translate('transferDetail.details') }}</h2>
        </div>
        <dl class="account-detail-grid">
          <div>
            <dt>{{ translate('transferDetail.inputMode') }}</dt>
            <dd>
              {{ modeLabels[transfer.mode] ? translate(modeLabels[transfer.mode]) : transfer.mode }}
            </dd>
          </div>
          <div>
            <dt>{{ translate('transferDetail.exchangeRate') }}</dt>
            <dd>{{ transfer.rate || '—' }}</dd>
          </div>
          <div>
            <dt>{{ translate('transfers.fee') }}</dt>
            <dd>
              {{
                transfer.feeAmount
                  ? `${amount(transfer.feeAmount)} ${transfer.feeCurrencyCode}`
                  : '—'
              }}
            </dd>
          </div>
          <div>
            <dt>{{ translate('transferDetail.feeAccount') }}</dt>
            <dd>{{ accountName(transfer.feeAccountId) }}</dd>
          </div>
          <div>
            <dt>{{ translate('ledger.created') }}</dt>
            <dd>{{ displayTime(transfer.createdAt) }}</dd>
          </div>
          <div>
            <dt>{{ translate('transferDetail.outgoingLeg') }}</dt>
            <dd>
              <RouterLink
                :to="{
                  name: 'transaction-detail',
                  params: { transactionId: transfer.outgoingTransactionId },
                }"
                >{{
                  translate('ledger.transactionNumber', { id: transfer.outgoingTransactionId })
                }}</RouterLink
              >
            </dd>
          </div>
          <div>
            <dt>{{ translate('transferDetail.incomingLeg') }}</dt>
            <dd>
              <RouterLink
                :to="{
                  name: 'transaction-detail',
                  params: { transactionId: transfer.incomingTransactionId },
                }"
                >{{
                  translate('ledger.transactionNumber', { id: transfer.incomingTransactionId })
                }}</RouterLink
              >
            </dd>
          </div>
          <div v-if="transfer.adjustmentTransactionIds.length">
            <dt>{{ translate('transferDetail.adjustments') }}</dt>
            <dd>
              <RouterLink
                v-for="id in transfer.adjustmentTransactionIds"
                :key="id"
                :to="{ name: 'transaction-detail', params: { transactionId: id } }"
                >{{ translate('ledger.transactionNumber', { id }) }}</RouterLink
              >
            </dd>
          </div>
        </dl>
      </section>

      <section
        v-if="transfer.correctsTransferGroupId || transfer.correctedByTransferGroupId"
        class="correction-links"
      >
        <RouterLink
          v-if="transfer.correctsTransferGroupId"
          :to="{
            name: 'transfer-detail',
            params: { transferGroupId: transfer.correctsTransferGroupId },
          }"
        >
          {{ translate('transferDetail.original', { id: transfer.correctsTransferGroupId }) }}
        </RouterLink>
        <RouterLink
          v-if="transfer.correctedByTransferGroupId"
          :to="{
            name: 'transfer-detail',
            params: { transferGroupId: transfer.correctedByTransferGroupId },
          }"
        >
          {{ translate('transferDetail.replacement', { id: transfer.correctedByTransferGroupId }) }}
        </RouterLink>
      </section>

      <TransferFormDialog
        :open="correctionOpen"
        mode="correct"
        :transfer="transfer"
        :accounts="accounts.items"
        :time-zone="userContext.preference?.timezone ?? 'UTC'"
        :pending="correctionPending"
        :error="correctionError"
        @close="correctionOpen = false"
        @submit="submitCorrection"
      />
      <ModalDialog
        :open="deleteOpen"
        :title="translate('transferDetail.deleteTitle')"
        @close="deletePending ? undefined : (deleteOpen = false)"
      >
        <div class="transfer-delete-impact">
          <span>{{ accountName(transfer.sourceAccountId) }}</span>
          <span>{{ accountName(transfer.targetAccountId) }}</span>
          <span v-if="transfer.feeAccountId">{{ accountName(transfer.feeAccountId) }}</span>
        </div>
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
            {{ translate(deletePending ? 'ledger.deleting' : 'transferDetail.deleteTitle') }}
          </button>
        </footer>
      </ModalDialog>
    </template>
  </AppShell>
</template>
