import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import { exportTransactionsCsv, getReportAnalysis } from './report-api';

describe('report API contract', () => {
  it('preserves URL filters and authoritative decimal strings', async () => {
    let received = new URL('https://example.invalid');
    server.use(
      mockHttp.get('*/api/v1/reports/analysis', ({ request }) => {
        received = new URL(request.url);
        return HttpResponse.json({
          baseCurrency: 'CNY',
          valuationAt: '2026-07-18T00:00:00Z',
          rateStatus: 'historical',
          reportPeriodStart: '2026-01-01T00:00:00Z',
          reportPeriodEnd: '2026-07-18T00:00:00Z',
          dimension: 'account',
          dimensionOverlaps: false,
          netWorthTrend: [
            {
              period: '2026-07',
              totalAssets: '100.00000000',
              totalLiabilities: '-20.00000000',
              netWorth: '80.00000000',
              valuedAt: '2026-07-18T00:00:00Z',
              rateStatus: 'current',
            },
          ],
          breakdown: [{ key: 'account:1', label: 'Wallet', income: '10', expense: '2', net: '8' }],
        });
      }),
    );

    const result = await getReportAnalysis({
      startDate: '2026-01',
      endDate: '2026-07',
      dimension: 'account',
    });
    expect(received.searchParams.get('startDate')).toBe('2026-01');
    expect(received.searchParams.get('endDate')).toBe('2026-07');
    expect(received.searchParams.get('dimension')).toBe('account');
    expect(result.netWorthTrend[0]?.netWorth).toBe('80.00000000');
    expect(typeof result.breakdown[0]?.expense).toBe('string');
  });

  it('uses server filename and row count for CSV downloads', async () => {
    let received = new URL('https://example.invalid');
    server.use(
      mockHttp.get('*/api/v1/exports/transactions.csv', ({ request }) => {
        received = new URL(request.url);
        return new HttpResponse('id,amount\r\n1,10\r\n', {
          headers: {
            'Content-Type': 'text/csv; charset=utf-8',
            'Content-Disposition': 'attachment; filename="transactions-202607.csv"',
            'X-Export-Row-Count': '1',
          },
        });
      }),
    );

    const result = await exportTransactionsCsv({
      from: '2026-07-01T00:00:00Z',
      to: '2026-08-01T00:00:00Z',
    });
    expect(received.searchParams.get('from')).toBe('2026-07-01T00:00:00Z');
    expect(received.searchParams.get('to')).toBe('2026-08-01T00:00:00Z');
    expect(result.filename).toBe('transactions-202607.csv');
    expect(result.rowCount).toBe(1);
    expect(result.blob).toBeInstanceOf(Blob);
  });
});
