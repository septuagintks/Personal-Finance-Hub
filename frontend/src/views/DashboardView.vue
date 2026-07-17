<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { ArrowDownRight, ArrowUpRight, CircleAlert, RefreshCw, WalletCards } from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import { getDashboardSummary, type DashboardSummary } from '../services/dashboard-api';
import { ApiError, type ApiErrorShape } from '../services/api-error';
import { formatDecimalString, toChartRatios } from '../services/presentation';
import { useUserContextStore } from '../stores/user-context';

const userContext = useUserContextStore();
const summary = ref<DashboardSummary | null>(null);
const loading = ref(true);
const error = ref<ApiErrorShape | null>(null);
let requestController: AbortController | null = null;

const movementBars = computed(() => {
  const values = [summary.value?.monthlyIncome ?? '0', summary.value?.monthlyExpense ?? '0'];
  const ratios = toChartRatios(values);
  return [
    {
      label: 'Income',
      value: values[0],
      height: ratios[0] * 100,
      tone: 'teal',
    },
    {
      label: 'Expenses',
      value: values[1],
      height: ratios[1] * 100,
      tone: 'coral',
    },
  ];
});

const assetRows = computed(() => {
  const slices = summary.value?.assetDistribution ?? [];
  const ratios = toChartRatios(slices.map(({ amount }) => amount));
  return slices.map((slice, index) => ({ ...slice, width: Math.max(2, ratios[index] * 100) }));
});

const categoryRows = computed(() => {
  const slices = summary.value?.topExpenseCategories ?? [];
  const ratios = toChartRatios(slices.map(({ amount }) => amount));
  return slices.map((slice, index) => ({ ...slice, width: Math.max(2, ratios[index] * 100) }));
});

function formatNumber(value: string | undefined): string {
  if (!value) return '—';
  return formatDecimalString(value, userContext.preference?.locale ?? 'en-US');
}

async function load(): Promise<void> {
  requestController?.abort();
  const controller = new AbortController();
  requestController = controller;
  loading.value = true;
  error.value = null;
  summary.value = null;
  try {
    summary.value = await getDashboardSummary(controller.signal);
  } catch (reason) {
    if (controller.signal.aborted) return;
    error.value =
      reason instanceof ApiError
        ? reason.details
        : {
            status: 0,
            errorCode: 'UNKNOWN_ERROR',
            message: 'The dashboard could not be loaded.',
            traceId: '',
            fieldErrors: {},
            retryable: true,
          };
  } finally {
    if (requestController === controller) {
      requestController = null;
      loading.value = false;
    }
  }
}

onMounted(load);
watch(() => userContext.aggregationRevision, load);
onBeforeUnmount(() => requestController?.abort());
</script>

<template>
  <AppShell>
    <div class="content-header">
      <div>
        <p class="eyebrow">Overview / Current month</p>
        <h1>Good morning.</h1>
        <p class="content-header__copy">
          A concise view of what changed and where your attention can go next.
        </p>
      </div>
      <button class="button button--quiet" type="button" :disabled="loading" @click="load">
        <RefreshCw :size="16" :class="{ spin: loading }" /> Refresh
      </button>
    </div>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>Dashboard unavailable</strong>
        <p>{{ error.message }}</p>
        <small v-if="error.traceId">Trace {{ error.traceId }}</small>
      </div>
      <button
        v-if="error.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load"
      >
        Try again
      </button>
    </div>

    <section class="metric-grid" aria-label="Financial summary">
      <article class="metric-card metric-card--primary">
        <div class="metric-card__top"><span>Net position</span><WalletCards :size="18" /></div>
        <strong>{{ formatNumber(summary?.netWorth?.netWorth) }}</strong
        ><small>{{ summary?.baseCurrency ?? 'Awaiting ledger data' }}</small>
      </article>
      <article class="metric-card">
        <div class="metric-card__top">
          <span>Income</span><ArrowUpRight :size="18" class="icon-teal" />
        </div>
        <strong>{{ formatNumber(summary?.monthlyIncome) }}</strong
        ><small>Current month</small>
      </article>
      <article class="metric-card">
        <div class="metric-card__top">
          <span>Expenses</span><ArrowDownRight :size="18" class="icon-coral" />
        </div>
        <strong>{{ formatNumber(summary?.monthlyExpense) }}</strong
        ><small>Current month</small>
      </article>
      <article class="metric-card">
        <div class="metric-card__top">
          <span>Asset groups</span
          ><span class="metric-count">{{ summary?.assetDistribution?.length ?? '—' }}</span>
        </div>
        <strong class="metric-card__value-small">{{ summary ? 'Current' : '—' }}</strong
        ><small>Server grouping</small>
      </article>
    </section>

    <section class="dashboard-columns">
      <article class="panel panel--wide">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">Movement</p>
            <h2>Month at a glance</h2>
          </div>
          <span class="panel-meta">{{
            summary?.reportPeriodStart ? 'Server-calculated window' : 'Waiting for data'
          }}</span>
        </div>
        <div v-if="loading" class="skeleton-chart" aria-label="Loading chart"></div>
        <div
          v-else-if="summary"
          class="chart-placeholder"
          aria-label="Income and expense comparison"
        >
          <div class="comparison-bars">
            <div v-for="bar in movementBars" :key="bar.label" class="comparison-column">
              <span class="comparison-value">{{ bar.value }}</span
              ><span
                class="comparison-bar"
                :class="`comparison-bar--${bar.tone}`"
                :style="{ height: `${Math.max(4, bar.height)}%` }"
              ></span
              ><strong>{{ bar.label }}</strong>
            </div>
          </div>
        </div>
        <div v-else class="empty-state">
          <p>No movement to display yet.</p>
          <span>Your first confirmed transaction will appear here.</span>
        </div>
      </article>
      <article class="panel">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">Accounts</p>
            <h2>Asset distribution</h2>
          </div>
        </div>
        <div v-if="assetRows.length" class="summary-breakdown-list">
          <div v-for="slice in assetRows" :key="slice.label" class="summary-breakdown-row">
            <div>
              <strong>{{ slice.label }}</strong>
              <span>{{ formatNumber(slice.amount) }} {{ summary?.baseCurrency }}</span>
            </div>
            <span>{{ slice.percentage }}</span>
            <div class="summary-breakdown-track" aria-hidden="true">
              <span :style="{ width: `${slice.width}%` }"></span>
            </div>
          </div>
        </div>
        <div v-else class="empty-state"><p>No account distribution yet.</p></div>
      </article>
    </section>

    <section class="panel dashboard-category-panel" aria-labelledby="top-categories-title">
      <div class="panel-heading">
        <div>
          <p class="section-kicker">Spending</p>
          <h2 id="top-categories-title">Top expense categories</h2>
        </div>
        <RouterLink class="text-link" :to="{ name: 'reports' }">Open reports</RouterLink>
      </div>
      <div v-if="categoryRows.length" class="dashboard-category-grid">
        <div v-for="category in categoryRows" :key="category.categoryId ?? 'uncategorized'">
          <div class="dashboard-category-row">
            <strong>{{ category.categoryName || 'Uncategorized' }}</strong>
            <span>{{ formatNumber(category.amount) }} {{ summary?.baseCurrency }}</span>
            <small>{{ category.percentage }}</small>
          </div>
          <div class="summary-breakdown-track" aria-hidden="true">
            <span :style="{ width: `${category.width}%` }"></span>
          </div>
        </div>
      </div>
      <div v-else class="empty-state"><p>No expenses this month.</p></div>
    </section>

    <section class="dashboard-footnote">
      <span class="privacy-dot"></span
      ><span
        >All totals are calculated by the PFH service in your configured timezone. This view never
        invents missing data.</span
      >
    </section>
  </AppShell>
</template>
