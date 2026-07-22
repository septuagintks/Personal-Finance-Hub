import { flushPromises, shallowMount } from '@vue/test-utils';
import { createPinia, setActivePinia } from 'pinia';
import { HttpResponse, http as mockHttp } from 'msw';
import { createMemoryHistory, createRouter } from 'vue-router';
import { beforeEach, describe, expect, it } from 'vitest';
import { server } from '../test/server';
import { useUserContextStore } from '../stores/user-context';
import ReportsView from './ReportsView.vue';

function reportResponse() {
  return {
    baseCurrency: 'CNY',
    valuationAt: '2026-07-23T00:00:00Z',
    rateStatus: 'current',
    reportPeriodStart: '2026-07-01T00:00:00Z',
    reportPeriodEnd: '2026-08-01T00:00:00Z',
    dimension: 'root_category',
    dimensionOverlaps: false,
    netWorthTrend: [],
    breakdown: [],
  };
}

describe('ReportsView', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('normalizes the initial route before issuing one report request', async () => {
    let requests = 0;
    server.use(
      mockHttp.get('*/api/v1/reports/analysis', () => {
        requests += 1;
        return HttpResponse.json(reportResponse());
      }),
    );
    const router = createRouter({
      history: createMemoryHistory(),
      routes: [{ path: '/reports', name: 'reports', component: ReportsView }],
    });
    await router.push('/reports');
    await router.isReady();

    const wrapper = shallowMount(ReportsView, {
      global: {
        plugins: [router],
        stubs: {
          AppShell: { template: '<div><slot /></div>' },
          ReportChart: true,
        },
      },
    });
    await flushPromises();
    await flushPromises();

    expect(router.currentRoute.value.query).toMatchObject({
      startDate: expect.stringMatching(/^\d{4}-\d{2}$/),
      endDate: expect.stringMatching(/^\d{4}-\d{2}$/),
      dimension: 'root_category',
    });
    expect(requests).toBe(1);

    useUserContextStore().invalidateAggregates();
    await flushPromises();
    expect(requests).toBe(2);
    wrapper.unmount();
  });
});
