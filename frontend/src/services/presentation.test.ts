import { describe, expect, it } from 'vitest';
import { formatDecimalString, formatInstant, toChartRatios, toEnumOptions } from './presentation';

describe('presentation adapters', () => {
  it('formats large decimal strings without converting the financial value to Number', () => {
    expect(formatDecimalString('999999999999.12345678', 'en-US')).toBe('999,999,999,999.12345678');
    expect(formatDecimalString('-1234.555', 'en-US', 2)).toBe('-1,234.56');
    expect(formatDecimalString('-0.004', 'en-US', 2)).toBe('0');
  });

  it('applies each persisted number format independently from the locale', () => {
    expect(formatDecimalString('1234567.89', 'en-US', 8, '1,234.56')).toBe('1,234,567.89');
    expect(formatDecimalString('1234567.89', 'en-US', 8, '1.234,56')).toBe('1.234.567,89');
    expect(formatDecimalString('-1234567.89', 'en-US', 8, '1 234,56')).toBe('-1 234 567,89');
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

  it('applies the persisted date format without losing locale-aware time', () => {
    const instant = '2026-07-01T00:30:00Z';
    const yearFirst = formatInstant(instant, {
      locale: 'en-US',
      timeZone: 'Asia/Shanghai',
      dateFormat: 'YYYY-MM-DD',
    });
    const dayFirst = formatInstant(instant, {
      locale: 'en-US',
      timeZone: 'Asia/Shanghai',
      dateFormat: 'dd/MM/yyyy',
    });
    expect(yearFirst).toMatch(/^2026-07-01 /);
    expect(dayFirst).toMatch(/^01\/07\/2026 /);
    expect(yearFirst).toContain('8:30');
    expect(dayFirst).toContain('8:30');
  });

  it('disambiguates repeated local times across a daylight-saving transition', () => {
    const beforeFallback = formatInstant('2026-11-01T05:30:00Z', {
      locale: 'en-US',
      timeZone: 'America/New_York',
      dateFormat: 'YYYY-MM-DD',
    });
    const afterFallback = formatInstant('2026-11-01T06:30:00Z', {
      locale: 'en-US',
      timeZone: 'America/New_York',
      dateFormat: 'YYYY-MM-DD',
    });

    expect(beforeFallback).toContain('2026-11-01 1:30');
    expect(afterFallback).toContain('2026-11-01 1:30');
    expect(beforeFallback).toContain('GMT-4');
    expect(afterFallback).toContain('GMT-5');
    expect(beforeFallback).not.toBe(afterFallback);
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
