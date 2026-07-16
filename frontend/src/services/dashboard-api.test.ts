import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import { getDashboardSummary } from './dashboard-api';

const endpoint = '*/api/v1/reports/dashboard-summary';

describe('dashboard API contract', () => {
  it('preserves decimal strings from a successful response', async () => {
    server.use(
      mockHttp.get(endpoint, () =>
        HttpResponse.json({
          baseCurrency: 'CNY',
          netWorth: {
            baseCurrency: 'CNY',
            totalAssets: '100.00000000',
            totalLiabilities: '25.00000000',
            netWorth: '75.00000000',
            generatedAt: '2026-07-16T00:00:00Z',
          },
          monthlyIncome: '20.00000000',
          monthlyExpense: '5.00000000',
          assetDistribution: [],
          topExpenseCategories: [],
          reportPeriodStart: '2026-07-01T00:00:00Z',
          reportPeriodEnd: '2026-08-01T00:00:00Z',
          generatedAt: '2026-07-16T00:00:00Z',
        }),
      ),
    );

    const result = await getDashboardSummary();
    expect(result.netWorth.netWorth).toBe('75.00000000');
    expect(typeof result.monthlyExpense).toBe('string');
  });

  it.each([
    [401, 'UNAUTHORIZED', false],
    [409, 'CONFLICT', false],
    [422, 'VALIDATION_ERROR', false],
    [500, 'INTERNAL_ERROR', true],
  ])('projects a %i response into the shared API error', async (status, errorCode, retryable) => {
    server.use(
      mockHttp.get(endpoint, () =>
        HttpResponse.json(
          {
            error_code: errorCode,
            message: 'Controlled failure.',
            trace_id: `trace-${status}`,
            retryable,
            field_errors:
              status === 422
                ? [{ field: 'timezone', code: 'invalid', message: 'Invalid timezone.' }]
                : [],
          },
          { status },
        ),
      ),
    );

    await expect(getDashboardSummary()).rejects.toMatchObject({
      details: {
        status,
        errorCode,
        traceId: `trace-${status}`,
        retryable,
      },
    });
  });
});
