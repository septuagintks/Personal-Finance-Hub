<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { ArrowDownRight, ArrowUpRight, CircleAlert, RefreshCw, WalletCards } from '@lucide/vue';
import AppShell from '../components/AppShell.vue';
import { getDashboardSummary, type DashboardSummary } from '../services/dashboard-api';
import { ApiError, type ApiErrorShape } from '../services/api-error';
import { formatDecimalString, toChartRatios } from '../services/presentation';
import { useUserContextStore } from '../stores/user-context';
import { translate } from '../i18n';

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
      label: translate('dashboard.income'),
      value: values[0],
      height: ratios[0] * 100,
      tone: 'teal',
    },
    {
      label: translate('dashboard.expenses'),
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
  return formatDecimalString(
    value,
    userContext.preference?.locale ?? 'en-US',
    8,
    userContext.preference?.numberFormat,
  );
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
            message: translate('dashboard.loadFailed'),
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
        <p class="eyebrow">{{ translate('dashboard.eyebrow') }}</p>
        <h1>{{ translate('dashboard.greeting') }}</h1>
        <p class="content-header__copy">
          {{ translate('dashboard.description') }}
        </p>
      </div>
      <button class="button button--quiet" type="button" :disabled="loading" @click="load">
        <RefreshCw :size="16" :class="{ spin: loading }" /> {{ translate('common.refresh') }}
      </button>
    </div>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{ translate('dashboard.unavailable') }}</strong>
        <p>{{ error.message }}</p>
        <small v-if="error.traceId">{{
          translate('accounts.trace', { traceId: error.traceId })
        }}</small>
      </div>
      <button
        v-if="error.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load"
      >
        {{ translate('ledger.tryAgain') }}
      </button>
    </div>

    <section class="metric-grid" :aria-label="translate('dashboard.financialSummary')">
      <article class="metric-card metric-card--primary">
        <div class="metric-card__top">
          <span>{{ translate('dashboard.netPosition') }}</span
          ><WalletCards :size="18" />
        </div>
        <strong>{{ formatNumber(summary?.netWorth?.netWorth) }}</strong
        ><small>{{ summary?.baseCurrency ?? translate('dashboard.awaitingData') }}</small>
      </article>
      <article class="metric-card">
        <div class="metric-card__top">
          <span>{{ translate('dashboard.income') }}</span
          ><ArrowUpRight :size="18" class="icon-teal" />
        </div>
        <strong>{{ formatNumber(summary?.monthlyIncome) }}</strong
        ><small>{{ translate('dashboard.currentMonth') }}</small>
      </article>
      <article class="metric-card">
        <div class="metric-card__top">
          <span>{{ translate('dashboard.expenses') }}</span
          ><ArrowDownRight :size="18" class="icon-coral" />
        </div>
        <strong>{{ formatNumber(summary?.monthlyExpense) }}</strong
        ><small>{{ translate('dashboard.currentMonth') }}</small>
      </article>
      <article class="metric-card">
        <div class="metric-card__top">
          <span>{{ translate('dashboard.assetGroups') }}</span
          ><span class="metric-count">{{ summary?.assetDistribution?.length ?? '—' }}</span>
        </div>
        <strong class="metric-card__value-small">{{
          summary ? translate('dashboard.current') : '—'
        }}</strong
        ><small>{{ translate('dashboard.serverGrouping') }}</small>
      </article>
    </section>

    <section class="dashboard-columns">
      <article class="panel panel--wide">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">{{ translate('dashboard.movement') }}</p>
            <h2>{{ translate('dashboard.monthGlance') }}</h2>
          </div>
          <span class="panel-meta">{{
            summary?.reportPeriodStart
              ? translate('dashboard.serverWindow')
              : translate('dashboard.waitingData')
          }}</span>
        </div>
        <div
          v-if="loading"
          class="skeleton-chart"
          :aria-label="translate('dashboard.loadingChart')"
        ></div>
        <div
          v-else-if="summary"
          class="chart-placeholder"
          :aria-label="translate('dashboard.comparison')"
        >
          <div class="comparison-bars">
            <div v-for="bar in movementBars" :key="bar.label" class="comparison-column">
              <span class="comparison-value">{{ formatNumber(bar.value) }}</span
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
          <p>{{ translate('dashboard.noMovement') }}</p>
          <span>{{ translate('dashboard.firstTransaction') }}</span>
        </div>
      </article>
      <article class="panel">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">{{ translate('dashboard.accounts') }}</p>
            <h2>{{ translate('dashboard.assetDistribution') }}</h2>
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
        <div v-else class="empty-state">
          <p>{{ translate('dashboard.noDistribution') }}</p>
        </div>
      </article>
    </section>

    <section class="panel dashboard-category-panel" aria-labelledby="top-categories-title">
      <div class="panel-heading">
        <div>
          <p class="section-kicker">{{ translate('dashboard.spending') }}</p>
          <h2 id="top-categories-title">{{ translate('dashboard.topCategories') }}</h2>
        </div>
        <RouterLink class="text-link" :to="{ name: 'reports' }">{{
          translate('dashboard.openReports')
        }}</RouterLink>
      </div>
      <div v-if="categoryRows.length" class="dashboard-category-grid">
        <div v-for="category in categoryRows" :key="category.categoryId ?? 'uncategorized'">
          <div class="dashboard-category-row">
            <strong>{{ category.categoryName || translate('ledger.uncategorized') }}</strong>
            <span>{{ formatNumber(category.amount) }} {{ summary?.baseCurrency }}</span>
            <small>{{ category.percentage }}</small>
          </div>
          <div class="summary-breakdown-track" aria-hidden="true">
            <span :style="{ width: `${category.width}%` }"></span>
          </div>
        </div>
      </div>
      <div v-else class="empty-state">
        <p>{{ translate('dashboard.noExpenses') }}</p>
      </div>
    </section>

    <section class="dashboard-footnote">
      <span class="privacy-dot"></span><span>{{ translate('dashboard.footnote') }}</span>
    </section>
  </AppShell>
</template>
