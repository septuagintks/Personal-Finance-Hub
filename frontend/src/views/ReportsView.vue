<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue';
import { CircleAlert, Download, RefreshCw } from '@lucide/vue';
import { useRoute, useRouter } from 'vue-router';
import AppShell from '../components/AppShell.vue';
import ReportChart from '../components/ReportChart.vue';
import { ApiError, type ApiErrorShape } from '../services/api-error';
import { formatDecimalString, formatInstant } from '../services/presentation';
import {
  exportTransactionsCsv,
  getReportAnalysis,
  type ReportAnalysis,
  type ReportDimension,
  type ReportFilters,
} from '../services/report-api';
import { useUserContextStore } from '../stores/user-context';
import { translate, type MessageKey } from '../i18n';
import { monthInTimeZone } from '../services/zoned-date-time';
import { resolveReportRange } from '../services/report-period';

const route = useRoute();
const router = useRouter();
const userContext = useUserContextStore();
const result = ref<ReportAnalysis | null>(null);
const loading = ref(false);
const exporting = ref(false);
const error = ref<ApiErrorShape | null>(null);
const startInput = ref('');
const endInput = ref('');
const dimensionInput = ref<ReportDimension>('root_category');
let requestController: AbortController | null = null;
let exportController: AbortController | null = null;
const pendingObjectUrls = new Map<string, number>();

const dimensions: Array<{ value: ReportDimension; label: MessageKey }> = [
  { value: 'root_category', label: 'reports.dimension.rootCategory' },
  { value: 'account', label: 'reports.dimension.account' },
  { value: 'tag', label: 'reports.dimension.tag' },
];

function currentMonth(): string {
  const timeZone = userContext.preference?.timezone ?? 'UTC';
  try {
    return monthInTimeZone(timeZone);
  } catch {
    return monthInTimeZone('UTC');
  }
}

function queryValue(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function routeFilters(): ReportFilters {
  const range = resolveReportRange(route.query, userContext.preference, currentMonth());
  const rawDimension = queryValue(route.query.dimension);
  const dimension = dimensions.some(({ value }) => value === rawDimension)
    ? (rawDimension as ReportDimension)
    : 'root_category';
  return { ...range, dimension };
}

function syncInputs(filters: ReportFilters): void {
  startInput.value = filters.startDate;
  endInput.value = filters.endDate;
  dimensionInput.value = filters.dimension;
}

function apiError(reason: unknown): ApiErrorShape {
  return reason instanceof ApiError
    ? reason.details
    : {
        status: 0,
        errorCode: 'UNKNOWN_ERROR',
        message: translate('reports.loadFailed'),
        traceId: '',
        fieldErrors: {},
        retryable: true,
      };
}

async function load(filters = routeFilters()): Promise<void> {
  syncInputs(filters);
  requestController?.abort();
  const controller = new AbortController();
  requestController = controller;
  loading.value = true;
  error.value = null;
  result.value = null;
  try {
    result.value = await getReportAnalysis(filters, controller.signal);
  } catch (reason) {
    if (!controller.signal.aborted) error.value = apiError(reason);
  } finally {
    if (requestController === controller) {
      requestController = null;
      loading.value = false;
    }
  }
}

function scheduleObjectUrlRelease(url: string): void {
  const timer = window.setTimeout(() => {
    URL.revokeObjectURL(url);
    pendingObjectUrls.delete(url);
  }, 0);
  pendingObjectUrls.set(url, timer);
}

async function applyFilters(): Promise<void> {
  if (!startInput.value || !endInput.value || startInput.value > endInput.value) return;
  await router.replace({
    name: 'reports',
    query: {
      startDate: startInput.value,
      endDate: endInput.value,
      dimension: dimensionInput.value,
    },
  });
}

async function exportCsv(): Promise<void> {
  if (!result.value || exporting.value) return;
  exportController?.abort();
  const controller = new AbortController();
  exportController = controller;
  exporting.value = true;
  error.value = null;
  try {
    const download = await exportTransactionsCsv(
      { from: result.value.reportPeriodStart, to: result.value.reportPeriodEnd },
      controller.signal,
    );
    if (controller.signal.aborted) return;
    const url = URL.createObjectURL(download.blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = download.filename;
    document.body.append(anchor);
    anchor.click();
    anchor.remove();
    scheduleObjectUrlRelease(url);
  } catch (reason) {
    if (!controller.signal.aborted) error.value = apiError(reason);
  } finally {
    if (exportController === controller) {
      exportController = null;
      exporting.value = false;
    }
  }
}

const locale = computed(() => userContext.preference?.locale ?? 'en-US');
const presentationLocale = computed(() => ({
  locale: locale.value,
  timeZone: userContext.preference?.timezone ?? 'UTC',
  dateFormat: userContext.preference?.dateFormat,
}));
const dimensionLabel = computed(() =>
  translate(
    dimensions.find(({ value }) => value === result.value?.dimension)?.label ??
      'reports.dimension.fallback',
  ),
);
const trendLabels = computed(() => result.value?.netWorthTrend.map(({ period }) => period) ?? []);
const trendSeries = computed(() => [
  {
    name: translate('reports.netWorthSeries'),
    values: result.value?.netWorthTrend.map(({ netWorth }) => netWorth) ?? [],
    color: '#267d72',
  },
]);
const breakdownLabels = computed(() => result.value?.breakdown.map(({ label }) => label) ?? []);
const breakdownSeries = computed(() => [
  {
    name: translate('dashboard.income'),
    values: result.value?.breakdown.map(({ income }) => income) ?? [],
    color: '#267d72',
  },
  {
    name: translate('ledger.type.expense'),
    values: result.value?.breakdown.map(({ expense }) => expense) ?? [],
    color: '#d36b5f',
  },
]);

function amount(value: string): string {
  return formatDecimalString(value, locale.value, 8, userContext.preference?.numberFormat);
}

function valuation(value: string): string {
  return formatInstant(value, presentationLocale.value);
}

async function synchronizeRoute(): Promise<void> {
  const filters = routeFilters();
  if (
    route.query.startDate !== filters.startDate ||
    route.query.endDate !== filters.endDate ||
    route.query.dimension !== filters.dimension
  ) {
    await router.replace({
      name: 'reports',
      query: {
        startDate: filters.startDate,
        endDate: filters.endDate,
        dimension: filters.dimension,
      },
    });
    return;
  }
  await load(filters);
}

watch(
  () => [
    route.query.startDate,
    route.query.endDate,
    route.query.dimension,
    userContext.aggregationRevision,
  ],
  () => void synchronizeRoute(),
  { immediate: true },
);

onBeforeUnmount(() => {
  requestController?.abort();
  exportController?.abort();
  for (const [url, timer] of pendingObjectUrls) {
    window.clearTimeout(timer);
    URL.revokeObjectURL(url);
  }
  pendingObjectUrls.clear();
});
</script>

<template>
  <AppShell>
    <div class="content-header report-header">
      <div>
        <p class="eyebrow">
          {{ translate('reports.eyebrow', { start: startInput, end: endInput }) }}
        </p>
        <h1>{{ translate('reports.title') }}</h1>
      </div>
      <button
        class="button button--quiet"
        type="button"
        :disabled="!result || exporting"
        @click="exportCsv"
      >
        <Download :size="16" />
        {{ translate(exporting ? 'reports.preparing' : 'reports.exportCsv') }}
      </button>
    </div>

    <form
      class="report-toolbar"
      :aria-label="translate('reports.filters')"
      @submit.prevent="applyFilters"
    >
      <label class="field">
        <span>{{ translate('reports.fromMonth') }}</span>
        <input v-model="startInput" type="month" :max="endInput || undefined" required />
      </label>
      <label class="field">
        <span>{{ translate('reports.toMonth') }}</span>
        <input v-model="endInput" type="month" :min="startInput" required />
      </label>
      <label class="field">
        <span>{{ translate('reports.breakdown') }}</span>
        <select v-model="dimensionInput">
          <option v-for="item in dimensions" :key="item.value" :value="item.value">
            {{ translate(item.label) }}
          </option>
        </select>
      </label>
      <button class="button" type="submit" :disabled="loading">
        <RefreshCw :size="16" :class="{ spin: loading }" /> {{ translate('ledger.apply') }}
      </button>
    </form>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{
          error.errorCode === 'INVALID_EXCHANGE_RATE'
            ? translate('reports.valuationUnavailable')
            : translate('reports.unavailable')
        }}</strong>
        <p>{{ error.message }}</p>
        <small v-if="error.traceId">{{
          translate('accounts.trace', { traceId: error.traceId })
        }}</small>
      </div>
      <button
        v-if="error.retryable"
        class="button button--small button--quiet"
        type="button"
        @click="load()"
      >
        {{ translate('ledger.tryAgain') }}
      </button>
    </div>

    <div v-if="loading" class="report-loading" :aria-label="translate('reports.loading')">
      <div class="skeleton-chart"></div>
      <div class="skeleton-chart"></div>
    </div>

    <template v-else-if="result">
      <dl class="report-meta" :aria-label="translate('reports.valuationLabel')">
        <div>
          <dt>{{ translate('reports.baseCurrency') }}</dt>
          <dd>{{ result.baseCurrency }}</dd>
        </div>
        <div>
          <dt>{{ translate('reports.valuation') }}</dt>
          <dd>{{ valuation(result.valuationAt) }}</dd>
        </div>
        <div>
          <dt>{{ translate('reports.rateStatus') }}</dt>
          <dd>
            <span
              class="status-badge"
              :class="{ 'status-badge--archived': result.rateStatus === 'historical' }"
            >
              {{
                translate(
                  result.rateStatus === 'historical' ? 'reports.historical' : 'reports.current',
                )
              }}
            </span>
          </dd>
        </div>
      </dl>

      <section class="report-section" aria-labelledby="net-worth-trend-title">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">{{ translate('reports.position') }}</p>
            <h2 id="net-worth-trend-title">{{ translate('reports.netWorthTrend') }}</h2>
          </div>
          <span class="panel-meta">{{ result.baseCurrency }}</span>
        </div>
        <ReportChart
          kind="line"
          :labels="trendLabels"
          :series="trendSeries"
          :accessible-label="translate('reports.netWorthChart')"
        />
        <div
          class="report-table-wrap"
          role="region"
          :aria-label="translate('reports.netWorthData')"
          tabindex="0"
        >
          <table class="report-table">
            <thead>
              <tr>
                <th>{{ translate('reports.period') }}</th>
                <th>{{ translate('reports.assets') }}</th>
                <th>{{ translate('reports.liabilities') }}</th>
                <th>{{ translate('reports.netWorth') }}</th>
                <th>{{ translate('reports.rate') }}</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="point in result.netWorthTrend" :key="point.period">
                <th scope="row">{{ point.period }}</th>
                <td>{{ amount(point.totalAssets) }}</td>
                <td>{{ amount(point.totalLiabilities) }}</td>
                <td>{{ amount(point.netWorth) }}</td>
                <td>
                  {{
                    translate(
                      point.rateStatus === 'historical' ? 'reports.historical' : 'reports.current',
                    )
                  }}
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </section>

      <section class="report-section" aria-labelledby="breakdown-title">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">{{ translate('reports.cashFlow') }}</p>
            <h2 id="breakdown-title">
              {{ translate('reports.breakdownTitle', { dimension: dimensionLabel }) }}
            </h2>
          </div>
          <span v-if="result.dimensionOverlaps" class="panel-meta">{{
            translate('reports.overlapping')
          }}</span>
        </div>
        <div v-if="result.breakdown.length === 0" class="empty-state">
          <p>{{ translate('reports.noActivity') }}</p>
        </div>
        <template v-else>
          <ReportChart
            kind="bar"
            :labels="breakdownLabels"
            :series="breakdownSeries"
            :accessible-label="translate('reports.breakdownChart', { dimension: dimensionLabel })"
          />
          <div
            class="report-table-wrap"
            role="region"
            :aria-label="translate('reports.breakdownData', { dimension: dimensionLabel })"
            tabindex="0"
          >
            <table class="report-table">
              <thead>
                <tr>
                  <th>{{ dimensionLabel }}</th>
                  <th>{{ translate('dashboard.income') }}</th>
                  <th>{{ translate('ledger.type.expense') }}</th>
                  <th>{{ translate('reports.net') }}</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="slice in result.breakdown" :key="slice.key">
                  <th scope="row">{{ slice.label }}</th>
                  <td>{{ amount(slice.income) }}</td>
                  <td>{{ amount(slice.expense) }}</td>
                  <td>{{ amount(slice.net) }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </template>
      </section>
    </template>
  </AppShell>
</template>
