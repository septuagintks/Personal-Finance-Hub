<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue';
import Decimal from 'decimal.js';
import { ArrowRightLeft, LoaderCircle, Save } from '@lucide/vue';
import type { Account } from '../services/account-api';
import type { CreateTransferRequest, Transfer } from '../services/transfer-api';
import ModalDialog from './ModalDialog.vue';

type TransferMode = CreateTransferRequest['mode'];
type FeeSource = NonNullable<CreateTransferRequest['feeSource']>;

const props = withDefaults(
  defineProps<{
    open: boolean;
    mode: 'create' | 'correct';
    accounts: Account[];
    transfer?: Transfer | null;
    pending?: boolean;
    error?: string;
  }>(),
  { transfer: null, pending: false, error: '' },
);

const emit = defineEmits<{ close: []; submit: [value: CreateTransferRequest] }>();
const localError = ref('');
const form = reactive({
  sourceAccountId: 0,
  targetAccountId: 0,
  mode: 'BothAmounts' as TransferMode,
  outgoingAmount: '',
  incomingAmount: '',
  rate: '',
  feeSource: '' as FeeSource | '',
  feeAmount: '',
  feeAccountId: 0,
  description: '',
  occurredAt: '',
});

const activeAccounts = computed(() => {
  const historicalIds = new Set(
    props.mode === 'correct' && props.transfer
      ? [
          props.transfer.sourceAccountId,
          props.transfer.targetAccountId,
          props.transfer.feeAccountId,
        ].filter((id): id is number => id !== null)
      : [],
  );
  return props.accounts.filter(({ id, isArchived }) => !isArchived || historicalIds.has(id));
});
const source = computed(() => props.accounts.find(({ id }) => id === form.sourceAccountId));
const target = computed(() => props.accounts.find(({ id }) => id === form.targetAccountId));
const sameCurrency = computed(() =>
  Boolean(source.value && target.value && source.value.currencyCode === target.value.currencyCode),
);
const thirdPartyAccounts = computed(() =>
  activeAccounts.value.filter(
    ({ id }) => id !== form.sourceAccountId && id !== form.targetAccountId,
  ),
);
const feeAccount = computed(() => {
  if (form.feeSource === 'SourceAccount') return source.value;
  if (form.feeSource === 'TargetAccount') return target.value;
  if (form.feeSource === 'ThirdParty') {
    return props.accounts.find(({ id }) => id === form.feeAccountId);
  }
  return undefined;
});
const title = computed(() => (props.mode === 'create' ? 'Record transfer' : 'Correct transfer'));

function toLocalInput(value: string | undefined): string {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '';
  const local = new Date(date.getTime() - date.getTimezoneOffset() * 60_000);
  return local.toISOString().slice(0, 16);
}

watch(
  () => [props.open, props.mode, props.transfer, props.accounts] as const,
  ([open]) => {
    if (!open) return;
    localError.value = '';
    const existing = props.transfer;
    form.sourceAccountId = existing?.sourceAccountId ?? activeAccounts.value[0]?.id ?? 0;
    form.targetAccountId =
      existing?.targetAccountId ??
      activeAccounts.value.find(({ id }) => id !== form.sourceAccountId)?.id ??
      0;
    form.mode = existing?.mode ?? 'BothAmounts';
    form.outgoingAmount = existing?.outgoingAmount ?? '';
    form.incomingAmount = existing?.incomingAmount ?? '';
    form.rate = existing?.rate ?? '';
    form.feeSource = existing?.feeSource ?? '';
    form.feeAmount = existing?.feeAmount ?? '';
    form.feeAccountId = existing?.feeAccountId ?? 0;
    form.description = existing?.description ?? '';
    form.occurredAt = toLocalInput(existing?.occurredAt);
  },
  { immediate: true },
);

watch(sameCurrency, (same) => {
  if (same) form.mode = 'BothAmounts';
});

watch(
  () => [form.sourceAccountId, form.targetAccountId, form.feeSource] as const,
  () => {
    if (
      form.targetAccountId === form.sourceAccountId ||
      !activeAccounts.value.some(({ id }) => id === form.targetAccountId)
    ) {
      form.targetAccountId =
        activeAccounts.value.find(({ id }) => id !== form.sourceAccountId)?.id ?? 0;
    }
    if (
      form.feeSource === 'ThirdParty' &&
      !thirdPartyAccounts.value.some(({ id }) => id === form.feeAccountId)
    ) {
      form.feeAccountId = thirdPartyAccounts.value[0]?.id ?? 0;
    }
  },
);

function decimal(text: string): Decimal | null {
  if (!/^[0-9]+(?:\.[0-9]+)?$/.test(text.trim())) return null;
  try {
    const value = new Decimal(text);
    return value.isPositive() ? value : null;
  } catch {
    return null;
  }
}

const preview = computed(() => {
  const outgoing = decimal(form.outgoingAmount);
  const incoming = decimal(form.incomingAmount);
  const rate = decimal(form.rate);
  try {
    if (form.mode === 'OutgoingAndRate' && outgoing && rate) {
      return {
        label: 'Estimated incoming',
        value: outgoing.mul(rate).toDecimalPlaces(8, Decimal.ROUND_HALF_EVEN).toString(),
        currency: target.value?.currencyCode ?? '',
      };
    }
    if (form.mode === 'IncomingAndRate' && incoming && rate) {
      return {
        label: 'Estimated outgoing',
        value: incoming.div(rate).toDecimalPlaces(8, Decimal.ROUND_HALF_EVEN).toString(),
        currency: source.value?.currencyCode ?? '',
      };
    }
    if (form.mode === 'BothAmounts' && outgoing && incoming && !sameCurrency.value) {
      return {
        label: 'Derived rate',
        value: incoming.div(outgoing).toDecimalPlaces(10, Decimal.ROUND_HALF_EVEN).toString(),
        currency: `${target.value?.currencyCode ?? ''}/${source.value?.currencyCode ?? ''}`,
      };
    }
  } catch {
    return null;
  }
  return null;
});

function setMode(mode: TransferMode): void {
  if (sameCurrency.value && mode !== 'BothAmounts') return;
  form.mode = mode;
}

function submit(): void {
  localError.value = '';
  if (!source.value || !target.value || source.value.id === target.value.id) {
    localError.value = 'Choose two different accounts.';
    return;
  }
  const outgoing = decimal(form.outgoingAmount);
  const incoming = decimal(form.incomingAmount);
  const rate = decimal(form.rate);
  if (
    (form.mode === 'OutgoingAndRate' && (!outgoing || !rate)) ||
    (form.mode === 'BothAmounts' && (!outgoing || !incoming)) ||
    (form.mode === 'IncomingAndRate' && (!incoming || !rate))
  ) {
    localError.value = 'Enter valid positive decimal values.';
    return;
  }
  if (sameCurrency.value && (!outgoing || !incoming || !outgoing.eq(incoming))) {
    localError.value = 'Same-currency transfer amounts must match.';
    return;
  }
  const fee = form.feeSource ? decimal(form.feeAmount) : null;
  if (form.feeSource && !fee) {
    localError.value = 'Enter a valid positive fee.';
    return;
  }
  if (form.feeSource === 'ThirdParty' && !feeAccount.value) {
    localError.value = 'Choose a fee account.';
    return;
  }
  emit('submit', {
    sourceAccountId: source.value.id,
    targetAccountId: target.value.id,
    mode: form.mode,
    outgoingAmount:
      form.mode === 'OutgoingAndRate' || form.mode === 'BothAmounts'
        ? form.outgoingAmount.trim()
        : null,
    incomingAmount:
      form.mode === 'IncomingAndRate' || form.mode === 'BothAmounts'
        ? form.incomingAmount.trim()
        : null,
    rate: form.mode === 'BothAmounts' ? null : form.rate.trim(),
    feeAmount: form.feeSource ? form.feeAmount.trim() : null,
    feeSource: form.feeSource || null,
    feeAccountId: form.feeSource === 'ThirdParty' ? form.feeAccountId : null,
    description: form.description.trim() || null,
    occurredAt: form.occurredAt ? new Date(form.occurredAt).toISOString() : null,
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
    <form class="transfer-form" novalidate @submit.prevent="submit">
      <div v-if="error || localError" class="form-alert" role="alert">
        {{ error || localError }}
      </div>
      <div class="transfer-mode" aria-label="Transfer input mode">
        <button
          v-for="option in [
            { value: 'OutgoingAndRate', label: 'Send + rate' },
            { value: 'BothAmounts', label: 'Both amounts' },
            { value: 'IncomingAndRate', label: 'Receive + rate' },
          ] as const"
          :key="option.value"
          type="button"
          :class="{ 'is-active': form.mode === option.value }"
          :aria-pressed="form.mode === option.value"
          :disabled="pending || (sameCurrency && option.value !== 'BothAmounts')"
          @click="setMode(option.value)"
        >
          {{ option.label }}
        </button>
      </div>

      <div class="transfer-form__grid">
        <label class="field">
          <span>From account</span>
          <select v-model="form.sourceAccountId" :disabled="pending">
            <option v-for="account in activeAccounts" :key="account.id" :value="account.id">
              {{ account.name }} · {{ account.currencyCode }}
            </option>
          </select>
        </label>
        <label class="field">
          <span>To account</span>
          <select v-model="form.targetAccountId" :disabled="pending">
            <option
              v-for="account in activeAccounts.filter(({ id }) => id !== form.sourceAccountId)"
              :key="account.id"
              :value="account.id"
            >
              {{ account.name }} · {{ account.currencyCode }}
            </option>
          </select>
        </label>
        <label v-if="form.mode !== 'IncomingAndRate'" class="field">
          <span>Outgoing amount · {{ source?.currencyCode }}</span>
          <input
            v-model="form.outgoingAmount"
            inputmode="decimal"
            maxlength="128"
            :disabled="pending"
          />
        </label>
        <label v-if="form.mode !== 'OutgoingAndRate'" class="field">
          <span>Incoming amount · {{ target?.currencyCode }}</span>
          <input
            v-model="form.incomingAmount"
            inputmode="decimal"
            maxlength="128"
            :disabled="pending"
          />
        </label>
        <label v-if="form.mode !== 'BothAmounts'" class="field">
          <span>Exchange rate</span>
          <input v-model="form.rate" inputmode="decimal" maxlength="128" :disabled="pending" />
        </label>
        <div v-if="preview" class="transfer-preview" aria-live="polite">
          <span>{{ preview.label }}</span>
          <strong>{{ preview.value }} {{ preview.currency }}</strong>
        </div>
        <label class="field">
          <span>Fee source</span>
          <select v-model="form.feeSource" :disabled="pending">
            <option value="">No fee</option>
            <option value="SourceAccount">From account</option>
            <option value="TargetAccount">To account</option>
            <option value="ThirdParty" :disabled="!thirdPartyAccounts.length">
              Another account
            </option>
          </select>
        </label>
        <label v-if="form.feeSource === 'ThirdParty'" class="field">
          <span>Fee account</span>
          <select v-model="form.feeAccountId" name="transfer-fee-account" :disabled="pending">
            <option v-for="account in thirdPartyAccounts" :key="account.id" :value="account.id">
              {{ account.name }} · {{ account.currencyCode }}
            </option>
          </select>
        </label>
        <label v-if="form.feeSource" class="field">
          <span>Fee amount · {{ feeAccount?.currencyCode }}</span>
          <input v-model="form.feeAmount" inputmode="decimal" maxlength="128" :disabled="pending" />
        </label>
        <label class="field">
          <span>Occurred at</span>
          <input v-model="form.occurredAt" type="datetime-local" :disabled="pending" />
        </label>
        <label class="field transfer-form__description">
          <span>Description</span>
          <textarea
            v-model="form.description"
            rows="3"
            maxlength="4096"
            :disabled="pending"
          ></textarea>
        </label>
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
        <button class="button" type="submit" :disabled="pending || activeAccounts.length < 2">
          <LoaderCircle v-if="pending" class="spin" :size="17" />
          <Save v-else-if="mode === 'correct'" :size="17" />
          <ArrowRightLeft v-else :size="17" />
          {{ pending ? 'Saving' : mode === 'create' ? 'Record transfer' : 'Create correction' }}
        </button>
      </footer>
    </form>
  </ModalDialog>
</template>
