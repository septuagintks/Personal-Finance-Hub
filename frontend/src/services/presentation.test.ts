import { describe, expect, it } from 'vitest';
import { formatDecimalString, formatInstant, toChartRatios, toEnumOptions } from './presentation';

describe('presentation adapters', () => {
  it('formats large decimal strings without converting the financial value to Number', () => {
    expect(formatDecimalString('999999999999.12345678', 'en-US')).toBe('999,999,999,999.12345678');
    expect(formatDecimalString('-1234.555', 'en-US', 2)).toBe('-1,234.56');
    expect(formatDecimalString('-0.004', 'en-US', 2)).toBe('0');
  });

  it('uses an IANA timezone for instant presentation', () => {
    const instant = '2026-07-01T00:30:00Z';
    expect(formatInstant(instant, { locale: 'en-US', timeZone: 'America/Los_Angeles' })).toContain(
      'Jun 30, 2026',
    );
    expect(formatInstant(instant, { locale: 'en-US', timeZone: 'Asia/Shanghai' })).toContain(
      'Jul 1, 2026',
    );
  });

  it('converts only dimensionless chart ratios to finite numbers', () => {
    expect(toChartRatios(['100000000000000000000', '25000000000000000000'])).toEqual([1, 0.25]);
    expect(toChartRatios(['not-a-decimal', '0'])).toEqual([0, 0]);
  });

  it('creates stable typed enum options', () => {
    expect(
      toEnumOptions(['INCOME', 'EXPENSE'] as const, {
        INCOME: 'Income',
        EXPENSE: 'Expense',
      }),
    ).toEqual([
      { value: 'INCOME', label: 'Income' },
      { value: 'EXPENSE', label: 'Expense' },
    ]);
  });
});
