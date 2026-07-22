<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { Activity, CircleAlert, RefreshCw, RotateCcw, ServerCog } from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import { ApiError } from '../services/api-error';
import {
  getOperationsSummary,
  listDeadLetters,
  retryDeadLetter,
  type DeadLetterItem,
  type OperationsSummary,
} from '../services/operations-api';
import { formatInstant } from '../services/presentation';
import { createIntentKeyTracker } from '../services/idempotency';
import { useUserContextStore } from '../stores/user-context';
import { translate } from '../i18n';

const userContext = useUserContextStore();
const summary = ref<OperationsSummary | null>(null);
const deadLetters = ref<DeadLetterItem[]>([]);
const nextCursor = ref<string | null>(null);
const loading = ref(false);
const loadingMore = ref(false);
const retryingId = ref('');
const error = ref('');
const errorTraceId = ref('');
let requestController: AbortController | null = null;
let loadMoreController: AbortController | null = null;
let retryController: AbortController | null = null;
let requestGeneration = 0;
const retryIntent = createIntentKeyTracker('dead-letter');

const presentationLocale = computed(() => ({
  locale: userContext.preference?.locale ?? 'en-US',
  timeZone: userContext.preference?.timezone ?? 'UTC',
  dateFormat: userContext.preference?.dateFormat,
}));

function boundedCount(value: { count: number; saturated: boolean } | undefined): string {
  if (!value) return '0';
  return value.saturated ? `${value.count}+` : String(value.count);
}

const statusMetrics = computed(() => {
  const outbox = summary.value?.outbox;
  return [
    {
      label: translate('operations.pending'),
      value: boundedCount(outbox?.pending),
      tone: 'neutral',
    },
    {
      label: translate('operations.retrying'),
      value: boundedCount(outbox?.failed),
      tone: 'warning',
    },
    {
      label: translate('operations.deadLetter'),
      value: boundedCount(outbox?.deadLetter),
      tone: 'danger',
    },
    {
      label: translate('operations.expiredIdempotency'),
      value: boundedCount(summary.value?.expiredIdempotency),
      tone: 'teal',
    },
  ];
});

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

function displayTime(value: string | null): string {
  return value ? formatInstant(value, presentationLocale.value) : translate('operations.never');
}

async function load(): Promise<void> {
  const generation = ++requestGeneration;
  requestController?.abort();
  loadMoreController?.abort();
  loadMoreController = null;
  loadingMore.value = false;
  const controller = new AbortController();
  requestController = controller;
  loading.value = true;
  clearError();
  try {
    const [overview, page] = await Promise.all([
      getOperationsSummary(controller.signal),
      listDeadLetters(undefined, 50, controller.signal),
    ]);
    if (generation !== requestGeneration || controller.signal.aborted) return;
    summary.value = overview;
    deadLetters.value = page.items;
    nextCursor.value = page.nextCursor;
  } catch (reason) {
    if (generation === requestGeneration && !controller.signal.aborted) {
      showError(reason, translate('operations.loadFailed'));
    }
  } finally {
    if (requestController === controller) {
      requestController = null;
      loading.value = false;
    }
  }
}

async function loadMore(): Promise<void> {
  if (!nextCursor.value || loadingMore.value) return;
  const cursor = nextCursor.value;
  const generation = requestGeneration;
  loadMoreController?.abort();
  const controller = new AbortController();
  loadMoreController = controller;
  loadingMore.value = true;
  clearError();
  try {
    const page = await listDeadLetters(cursor, 50, controller.signal);
    if (generation !== requestGeneration || controller.signal.aborted) return;
    deadLetters.value = [...deadLetters.value, ...page.items];
    nextCursor.value = page.nextCursor;
  } catch (reason) {
    if (generation === requestGeneration && !controller.signal.aborted) {
      showError(reason, translate('operations.loadMoreFailed'));
    }
  } finally {
    if (loadMoreController === controller) {
      loadMoreController = null;
      loadingMore.value = false;
    }
  }
}

async function retry(item: DeadLetterItem): Promise<void> {
  if (retryingId.value) return;
  retryController?.abort();
  const controller = new AbortController();
  retryController = controller;
  retryingId.value = item.id;
  clearError();
  const intentKey = retryIntent.keyFor({ outboxId: item.id });
  try {
    await retryDeadLetter(item.id, intentKey, controller.signal);
    if (!controller.signal.aborted) {
      retryIntent.complete(intentKey);
      await load();
    }
  } catch (reason) {
    if (!controller.signal.aborted) showError(reason, translate('operations.retryFailed'));
  } finally {
    if (retryController === controller) {
      retryController = null;
      retryingId.value = '';
    }
  }
}

onMounted(load);
onBeforeUnmount(() => {
  requestGeneration += 1;
  requestController?.abort();
  loadMoreController?.abort();
  retryController?.abort();
  retryIntent.clear();
});
</script>

<template>
  <AppShell>
    <div class="content-header operations-header">
      <div>
        <p class="eyebrow">{{ translate('operations.eyebrow') }}</p>
        <h1>{{ translate('operations.title') }}</h1>
      </div>
      <button class="button button--quiet" type="button" :disabled="loading" @click="load">
        <RefreshCw :size="16" :class="{ spin: loading }" /> {{ translate('common.refresh') }}
      </button>
    </div>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('operations.requestFailed') }}</strong>
        <p>{{ error }}</p>
        <small v-if="errorTraceId">{{
          translate('maintenance.traceValue', { traceId: errorTraceId })
        }}</small>
      </div>
    </div>

    <section
      class="operations-metrics"
      :aria-label="translate('operations.summary')"
      :aria-busy="loading"
    >
      <article
        v-for="metric in statusMetrics"
        :key="metric.label"
        :class="`operations-metric operations-metric--${metric.tone}`"
      >
        <span>{{ metric.label }}</span>
        <strong>{{ metric.value }}</strong>
      </article>
    </section>

    <div class="operations-grid">
      <section class="operations-panel" aria-labelledby="jobs-heading">
        <div class="section-heading">
          <div>
            <p class="section-kicker">{{ translate('operations.scheduler') }}</p>
            <h2 id="jobs-heading">{{ translate('operations.jobs') }}</h2>
          </div>
          <Activity :size="21" aria-hidden="true" />
        </div>
        <div class="operations-table operations-table--jobs" tabindex="0">
          <div class="operations-table__head" aria-hidden="true">
            <span>{{ translate('operations.job') }}</span
            ><span>{{ translate('operations.state') }}</span
            ><span>{{ translate('operations.lastResult') }}</span
            ><span>{{ translate('operations.duration') }}</span>
          </div>
          <div v-for="job in summary?.jobs ?? []" :key="job.name" class="operations-table__row">
            <strong>{{ job.name }}</strong>
            <span :class="['runtime-state', { 'runtime-state--running': job.running }]">{{
              translate(
                job.running
                  ? 'operations.running'
                  : job.schedulerStarted
                    ? 'operations.ready'
                    : 'operations.stopped',
              )
            }}</span>
            <span>{{ job.lastResult }} · {{ displayTime(job.lastFinishedAt) }}</span>
            <code>{{ translate('operations.durationMs', { value: job.lastDurationMs }) }}</code>
          </div>
          <div v-if="summary && !summary.jobs.length" class="empty-state">
            <p>{{ translate('operations.noJobs') }}</p>
          </div>
        </div>
      </section>

      <section class="operations-panel" aria-labelledby="leases-heading">
        <div class="section-heading">
          <div>
            <p class="section-kicker">{{ translate('operations.coordination') }}</p>
            <h2 id="leases-heading">{{ translate('operations.leases') }}</h2>
          </div>
          <ServerCog :size="21" aria-hidden="true" />
        </div>
        <div class="lease-list">
          <div v-for="lease in summary?.leases ?? []" :key="lease.jobName" class="lease-list__row">
            <strong>{{ lease.jobName }}</strong>
            <span :class="['status-badge', { 'status-badge--archived': !lease.active }]">{{
              translate(lease.active ? 'operations.active' : 'operations.expired')
            }}</span>
            <time :datetime="lease.leaseUntil">{{ displayTime(lease.leaseUntil) }}</time>
          </div>
          <div v-if="summary && !summary.leases.length" class="empty-state">
            <p>{{ translate('operations.noLeases') }}</p>
          </div>
        </div>
        <dl v-if="summary" class="operations-facts">
          <div>
            <dt>{{ translate('operations.handlerReceipts') }}</dt>
            <dd>{{ boundedCount(summary.handlerReceipts) }}</dd>
          </div>
          <div>
            <dt>{{ translate('operations.latestReceipt') }}</dt>
            <dd>{{ displayTime(summary.handlerReceipts.latestAt) }}</dd>
          </div>
          <div>
            <dt>{{ translate('operations.windowStart') }}</dt>
            <dd>{{ displayTime(summary.windowStart) }}</dd>
          </div>
          <div>
            <dt>{{ translate('operations.generated') }}</dt>
            <dd>{{ displayTime(summary.generatedAt) }}</dd>
          </div>
        </dl>
      </section>
    </div>

    <section class="dead-letter-section" aria-labelledby="dead-letter-heading">
      <div class="section-heading">
        <div>
          <p class="section-kicker">{{ translate('operations.outbox') }}</p>
          <h2 id="dead-letter-heading">{{ translate('operations.deadLetters') }}</h2>
        </div>
      </div>
      <div class="dead-letter-list" :aria-busy="loading">
        <div class="dead-letter-list__head" aria-hidden="true">
          <span>{{ translate('operations.event') }}</span
          ><span>{{ translate('operations.aggregate') }}</span
          ><span>{{ translate('operations.attempts') }}</span
          ><span>{{ translate('operations.lastFailure') }}</span
          ><span>{{ translate('maintenance.action') }}</span>
        </div>
        <div v-for="item in deadLetters" :key="item.id" class="dead-letter-list__row">
          <div>
            <strong>{{ item.eventName }}</strong
            ><code>{{ item.id }}</code>
          </div>
          <span
            >{{ item.aggregateType ?? translate('operations.unscoped')
            }}<small>{{ item.aggregateId ?? translate('operations.noAggregateId') }}</small></span
          >
          <code>{{ item.retryCount }} / {{ item.maxRetryCount }}</code>
          <span
            >{{ displayTime(item.lastFailedAt)
            }}<small>{{
              item.lastFailedHandler ?? translate('operations.unknownHandler')
            }}</small></span
          >
          <button
            class="button button--quiet button--small"
            type="button"
            :disabled="Boolean(retryingId)"
            @click="retry(item)"
          >
            <RotateCcw :size="15" />
            {{ translate(retryingId === item.id ? 'operations.scheduling' : 'common.retry') }}
          </button>
        </div>
        <div v-if="!loading && !deadLetters.length" class="empty-state">
          <p>{{ translate('operations.noDeadLetters') }}</p>
          <span>{{ translate('operations.noDeadLettersDetail') }}</span>
        </div>
      </div>
      <div v-if="nextCursor" class="ledger-load-more">
        <button
          class="button button--quiet"
          type="button"
          :disabled="loadingMore"
          @click="loadMore"
        >
          {{ translate('ledger.loadMore') }}
        </button>
      </div>
    </section>
  </AppShell>
</template>
