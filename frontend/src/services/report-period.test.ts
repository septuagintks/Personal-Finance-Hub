import { describe, expect, it } from 'vitest';
import { preferredReportRange, resolveReportRange } from './report-period';

const preference = (
  defaultReportPeriod: 'current_month' | 'last_month' | 'last_3_months' | 'current_year' | 'custom',
  start: string | null = null,
  end: string | null = null,
) => ({
  defaultReportPeriod,
  customReportStartMonth: start,
  customReportEndMonth: end,
});

describe('default report periods', () => {
  it.each([
    ['current_month', { startDate: '2025-05', endDate: '2025-05' }],
    ['last_month', { startDate: '2025-04', endDate: '2025-04' }],
    ['last_3_months', { startDate: '2025-03', endDate: '2025-05' }],
    ['current_year', { startDate: '2025-01', endDate: '2025-05' }],
  ] as const)('resolves %s', (period, expected) => {
    expect(preferredReportRange(preference(period), '2025-05')).toEqual(expected);
  });

  it('uses the persisted custom range, including a future end month', () => {
    expect(preferredReportRange(preference('custom', '2025-02', '2025-08'), '2025-05')).toEqual({
      startDate: '2025-02',
      endDate: '2025-08',
    });
  });

  it('falls back safely when a custom preference is incomplete', () => {
    expect(preferredReportRange(preference('custom', '2025-02'), '2025-05')).toEqual({
      startDate: '2025-05',
      endDate: '2025-05',
    });
  });

  it('gives a valid explicit URL range precedence over preferences', () => {
    expect(
      resolveReportRange(
        { startDate: '2024-01', endDate: '2024-12' },
        preference('last_month'),
        '2025-05',
      ),
    ).toEqual({ startDate: '2024-01', endDate: '2024-12' });
  });
});
