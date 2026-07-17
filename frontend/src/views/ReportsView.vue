<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
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

const dimensions: Array<{ value: ReportDimension; label: string }> = [
  { value: 'root_category', label: 'Root category' },
  { value: 'account', label: 'Account' },
  { value: 'tag', label: 'Tag' },
];

function currentMonth(): string {
  const timeZone = userContext.preference?.timezone ?? 'UTC';
  try {
    const parts = new Intl.DateTimeFormat('en-US', {
      timeZone,
      year: 'numeric',
      month: '2-digit',
    }).formatToParts(new Date());
    const year = parts.find(({ type }) => type === 'year')?.value;
    const month = parts.find(({ type }) => type === 'month')?.value;
    if (year && month) return `${year}-${month}`;
  } catch {
    // The server remains authoritative and will reject an unavailable timezone.
  }
  return new Date().toISOString().slice(0, 7);
}

function shiftMonth(month: string, offset: number): string {
  const [year, number] = month.split('-').map(Number);
  const date = new Date(Date.UTC(year, number - 1 + offset, 1));
  return date.toISOString().slice(0, 7);
}

function queryValue(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function routeFilters(): ReportFilters {
  const maximum = currentMonth();
  const defaultEnd = maximum;
  const defaultStart = shiftMonth(defaultEnd, -5);
  const rawStart = queryValue(route.query.startDate);
  const rawEnd = queryValue(route.query.endDate);
  const rawDimension = queryValue(route.query.dimension);
  const validMonth = (value: string) => /^\d{4}-(0[1-9]|1[0-2])$/.test(value);
  const startDate = validMonth(rawStart) && rawStart <= maximum ? rawStart : defaultStart;
  const endDate = validMonth(rawEnd) && rawEnd <= maximum ? rawEnd : defaultEnd;
  const dimension = dimensions.some(({ value }) => value === rawDimension)
    ? (rawDimension as ReportDimension)
    : 'root_category';
  if (startDate > endDate) {
    return { startDate: defaultStart, endDate: defaultEnd, dimension };
  }
  return { startDate, endDate, dimension };
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
        message: 'The report could not be loaded.',
        traceId: '',
        fieldErrors: {},
        retryable: true,
      };
}

async function load(): Promise<void> {
  const filters = routeFilters();
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
    window.setTimeout(() => URL.revokeObjectURL(url), 0);
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
const dimensionLabel = computed(
  () => dimensions.find(({ value }) => value === result.value?.dimension)?.label ?? 'Dimension',
);
const trendLabels = computed(() => result.value?.netWorthTrend.map(({ period }) => period) ?? []);
const trendSeries = computed(() => [
  {
    name: 'Net worth',
    values: result.value?.netWorthTrend.map(({ netWorth }) => netWorth) ?? [],
    color: '#267d72',
  },
]);
const breakdownLabels = computed(() => result.value?.breakdown.map(({ label }) => label) ?? []);
const breakdownSeries = computed(() => [
  {
    name: 'Income',
    values: result.value?.breakdown.map(({ income }) => income) ?? [],
    color: '#267d72',
  },
  {
    name: 'Expense',
    values: result.value?.breakdown.map(({ expense }) => expense) ?? [],
    color: '#d36b5f',
  },
]);

function amount(value: string): string {
  return formatDecimalString(value, locale.value);
}

function valuation(value: string): string {
  return formatInstant(value, presentationLocale.value);
}

watch(() => [route.query.startDate, route.query.endDate, route.query.dimension], load, {
  immediate: true,
});
watch(() => userContext.aggregationRevision, load);

onMounted(async () => {
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
  }
});

onBeforeUnmount(() => {
  requestController?.abort();
  exportController?.abort();
});
</script>

<template>
  <AppShell>
    <div class="content-header report-header">
      <div>
        <p class="eyebrow">Analysis / {{ startInput }} to {{ endInput }}</p>
        <h1>Reports</h1>
      </div>
      <button
        class="button button--quiet"
        type="button"
        :disabled="!result || exporting"
        @click="exportCsv"
      >
        <Download :size="16" /> {{ exporting ? 'Preparing' : 'Export CSV' }}
      </button>
    </div>

    <form class="report-toolbar" aria-label="Report filters" @submit.prevent="applyFilters">
      <label class="field">
        <span>From month</span>
        <input v-model="startInput" type="month" :max="endInput || currentMonth()" required />
      </label>
      <label class="field">
        <span>To month</span>
        <input v-model="endInput" type="month" :min="startInput" :max="currentMonth()" required />
      </label>
      <label class="field">
        <span>Breakdown</span>
        <select v-model="dimensionInput">
          <option v-for="item in dimensions" :key="item.value" :value="item.value">
            {{ item.label }}
          </option>
        </select>
      </label>
      <button class="button" type="submit" :disabled="loading">
        <RefreshCw :size="16" :class="{ spin: loading }" /> Apply
      </button>
    </form>

    <div v-if="error" class="page-alert" role="alert">
      <CircleAlert :size="19" />
      <div>
        <strong>{{
          error.errorCode === 'INVALID_EXCHANGE_RATE'
            ? 'Valuation unavailable'
            : 'Report unavailable'
        }}</strong>
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

    <div v-if="loading" class="report-loading" aria-label="Loading report">
      <div class="skeleton-chart"></div>
      <div class="skeleton-chart"></div>
    </div>

    <template v-else-if="result">
      <dl class="report-meta" aria-label="Report valuation">
        <div>
          <dt>Base currency</dt>
          <dd>{{ result.baseCurrency }}</dd>
        </div>
        <div>
          <dt>Valuation</dt>
          <dd>{{ valuation(result.valuationAt) }}</dd>
        </div>
        <div>
          <dt>Rate status</dt>
          <dd>
            <span
              class="status-badge"
              :class="{ 'status-badge--archived': result.rateStatus === 'historical' }"
            >
              {{ result.rateStatus === 'historical' ? 'Historical' : 'Current' }}
            </span>
          </dd>
        </div>
      </dl>

      <section class="report-section" aria-labelledby="net-worth-trend-title">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">Position</p>
            <h2 id="net-worth-trend-title">Net worth trend</h2>
          </div>
          <span class="panel-meta">{{ result.baseCurrency }}</span>
        </div>
        <ReportChart
          kind="line"
          :labels="trendLabels"
          :series="trendSeries"
          accessible-label="Monthly net worth trend"
        />
        <div class="report-table-wrap" role="region" aria-label="Net worth trend data" tabindex="0">
          <table class="report-table">
            <thead>
              <tr>
                <th>Period</th>
                <th>Assets</th>
                <th>Liabilities</th>
                <th>Net worth</th>
                <th>Rate</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="point in result.netWorthTrend" :key="point.period">
                <th scope="row">{{ point.period }}</th>
                <td>{{ amount(point.totalAssets) }}</td>
                <td>{{ amount(point.totalLiabilities) }}</td>
                <td>{{ amount(point.netWorth) }}</td>
                <td>{{ point.rateStatus === 'historical' ? 'Historical' : 'Current' }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </section>

      <section class="report-section" aria-labelledby="breakdown-title">
        <div class="panel-heading">
          <div>
            <p class="section-kicker">Cash flow</p>
            <h2 id="breakdown-title">{{ dimensionLabel }} breakdown</h2>
          </div>
          <span v-if="result.dimensionOverlaps" class="panel-meta">Overlapping</span>
        </div>
        <div v-if="result.breakdown.length === 0" class="empty-state">
          <p>No activity in this period.</p>
        </div>
        <template v-else>
          <ReportChart
            kind="bar"
            :labels="breakdownLabels"
            :series="breakdownSeries"
            :accessible-label="`${dimensionLabel} income and expense breakdown`"
          />
          <div
            class="report-table-wrap"
            role="region"
            :aria-label="`${dimensionLabel} breakdown data`"
            tabindex="0"
          >
            <table class="report-table">
              <thead>
                <tr>
                  <th>{{ dimensionLabel }}</th>
                  <th>Income</th>
                  <th>Expense</th>
                  <th>Net</th>
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
