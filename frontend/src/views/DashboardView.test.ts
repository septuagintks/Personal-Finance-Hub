import { flushPromises, shallowMount } from '@vue/test-utils';
import { HttpResponse, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import { createPinia, setActivePinia } from 'pinia';
import { server } from '../test/server';
import DashboardView from './DashboardView.vue';

const endpoint = '*/api/v1/reports/dashboard-summary';

describe('DashboardView', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('removes prior totals when a refresh fails', async () => {
    server.use(
      mockHttp.get(endpoint, () =>
        HttpResponse.json({
          baseCurrency: 'CNY',
          netWorth: {
            baseCurrency: 'CNY',
            totalAssets: '987654.32',
            totalLiabilities: '0',
            netWorth: '987654.32',
            generatedAt: '2026-07-16T00:00:00Z',
          },
          monthlyIncome: '20',
          monthlyExpense: '5',
          assetDistribution: [],
          topExpenseCategories: [],
          reportPeriodStart: '2026-07-01T00:00:00Z',
          reportPeriodEnd: '2026-08-01T00:00:00Z',
          generatedAt: '2026-07-16T00:00:00Z',
        }),
      ),
    );
    const wrapper = shallowMount(DashboardView, {
      global: {
        stubs: {
          AppShell: { template: '<div><slot /></div>' },
          RouterLink: { template: '<a><slot /></a>' },
        },
      },
    });
    await flushPromises();
    expect(wrapper.text()).toContain('987,654.32');

    server.use(
      mockHttp.get(endpoint, () =>
        HttpResponse.json(
          {
            error_code: 'VALIDATION_ERROR',
            message: 'A required exchange rate is unavailable.',
            trace_id: 'dashboard-refresh-failed',
            retryable: false,
            field_errors: [],
          },
          { status: 422 },
        ),
      ),
    );
    const refresh = wrapper.findAll('button').find((button) => button.text().includes('Refresh'));
    expect(refresh).toBeDefined();
    await refresh!.trigger('click');
    await flushPromises();

    expect(wrapper.text()).toContain('A required exchange rate is unavailable.');
    expect(wrapper.text()).not.toContain('987,654.32');
  });
});
