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
    { label: 'Pending', value: boundedCount(outbox?.pending), tone: 'neutral' },
    { label: 'Retrying', value: boundedCount(outbox?.failed), tone: 'warning' },
    { label: 'Dead letter', value: boundedCount(outbox?.deadLetter), tone: 'danger' },
    {
      label: 'Expired idempotency',
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
  return value ? formatInstant(value, presentationLocale.value) : 'Never';
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
      showError(reason, 'Operational status could not be loaded.');
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
      showError(reason, 'More dead letters could not be loaded.');
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
    if (!controller.signal.aborted) showError(reason, 'The dead letter could not be retried.');
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
        <p class="eyebrow">Runtime / Operator</p>
        <h1>Operations</h1>
      </div>
      <button class="button button--quiet" type="button" :disabled="loading" @click="load">
        <RefreshCw :size="16" :class="{ spin: loading }" /> Refresh
      </button>
    </div>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>Operations request failed</strong>
        <p>{{ error }}</p>
        <small v-if="errorTraceId">Trace {{ errorTraceId }}</small>
      </div>
    </div>

    <section class="operations-metrics" aria-label="Operational summary" :aria-busy="loading">
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
            <p class="section-kicker">Scheduler</p>
            <h2 id="jobs-heading">Jobs</h2>
          </div>
          <Activity :size="21" aria-hidden="true" />
        </div>
        <div class="operations-table operations-table--jobs" tabindex="0">
          <div class="operations-table__head" aria-hidden="true">
            <span>Job</span><span>State</span><span>Last result</span><span>Duration</span>
          </div>
          <div v-for="job in summary?.jobs ?? []" :key="job.name" class="operations-table__row">
            <strong>{{ job.name }}</strong>
            <span :class="['runtime-state', { 'runtime-state--running': job.running }]">{{
              job.running ? 'Running' : job.schedulerStarted ? 'Ready' : 'Stopped'
            }}</span>
            <span>{{ job.lastResult }} · {{ displayTime(job.lastFinishedAt) }}</span>
            <code>{{ job.lastDurationMs }} ms</code>
          </div>
          <div v-if="summary && !summary.jobs.length" class="empty-state">
            <p>No scheduler jobs</p>
          </div>
        </div>
      </section>

      <section class="operations-panel" aria-labelledby="leases-heading">
        <div class="section-heading">
          <div>
            <p class="section-kicker">Coordination</p>
            <h2 id="leases-heading">Leases</h2>
          </div>
          <ServerCog :size="21" aria-hidden="true" />
        </div>
        <div class="lease-list">
          <div v-for="lease in summary?.leases ?? []" :key="lease.jobName" class="lease-list__row">
            <strong>{{ lease.jobName }}</strong>
            <span :class="['status-badge', { 'status-badge--archived': !lease.active }]">{{
              lease.active ? 'Active' : 'Expired'
            }}</span>
            <time :datetime="lease.leaseUntil">{{ displayTime(lease.leaseUntil) }}</time>
          </div>
          <div v-if="summary && !summary.leases.length" class="empty-state">
            <p>No active leases</p>
          </div>
        </div>
        <dl v-if="summary" class="operations-facts">
          <div>
            <dt>Handler receipts</dt>
            <dd>{{ boundedCount(summary.handlerReceipts) }}</dd>
          </div>
          <div>
            <dt>Latest receipt</dt>
            <dd>{{ displayTime(summary.handlerReceipts.latestAt) }}</dd>
          </div>
          <div>
            <dt>Window start</dt>
            <dd>{{ displayTime(summary.windowStart) }}</dd>
          </div>
          <div>
            <dt>Generated</dt>
            <dd>{{ displayTime(summary.generatedAt) }}</dd>
          </div>
        </dl>
      </section>
    </div>

    <section class="dead-letter-section" aria-labelledby="dead-letter-heading">
      <div class="section-heading">
        <div>
          <p class="section-kicker">Outbox</p>
          <h2 id="dead-letter-heading">Dead letters</h2>
        </div>
      </div>
      <div class="dead-letter-list" :aria-busy="loading">
        <div class="dead-letter-list__head" aria-hidden="true">
          <span>Event</span><span>Aggregate</span><span>Attempts</span><span>Last failure</span
          ><span>Action</span>
        </div>
        <div v-for="item in deadLetters" :key="item.id" class="dead-letter-list__row">
          <div>
            <strong>{{ item.eventName }}</strong
            ><code>{{ item.id }}</code>
          </div>
          <span
            >{{ item.aggregateType ?? 'Unscoped'
            }}<small>{{ item.aggregateId ?? 'No aggregate ID' }}</small></span
          >
          <code>{{ item.retryCount }} / {{ item.maxRetryCount }}</code>
          <span
            >{{ displayTime(item.lastFailedAt)
            }}<small>{{ item.lastFailedHandler ?? 'Unknown handler' }}</small></span
          >
          <button
            class="button button--quiet button--small"
            type="button"
            :disabled="Boolean(retryingId)"
            @click="retry(item)"
          >
            <RotateCcw :size="15" /> {{ retryingId === item.id ? 'Scheduling' : 'Retry' }}
          </button>
        </div>
        <div v-if="!loading && !deadLetters.length" class="empty-state">
          <p>No dead letters</p>
          <span>The outbox has no terminal failures.</span>
        </div>
      </div>
      <div v-if="nextCursor" class="ledger-load-more">
        <button
          class="button button--quiet"
          type="button"
          :disabled="loadingMore"
          @click="loadMore"
        >
          Load more
        </button>
      </div>
    </section>
  </AppShell>
</template>
