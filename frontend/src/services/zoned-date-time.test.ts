import { describe, expect, it } from 'vitest';
import { Temporal } from '@js-temporal/polyfill';
import {
  ZonedDateTimeError,
  instantToLocalDateTime,
  localDateTimeToInstant,
  monthInTimeZone,
  naturalDayWindow,
  shiftReportMonth,
} from './zoned-date-time';

describe('zoned date-time service', () => {
  it('round-trips instants through the explicit preference timezone', () => {
    expect(instantToLocalDateTime('2025-01-01T00:00:00Z', 'Asia/Kathmandu')).toBe(
      '2025-01-01T05:45',
    );
    expect(localDateTimeToInstant('2025-01-01T05:45', 'Asia/Kathmandu')).toBe(
      '2025-01-01T00:00:00Z',
    );
  });

  it('rejects nonexistent DST wall times', () => {
    expect(() => localDateTimeToInstant('2025-03-09T02:30', 'America/New_York')).toThrowError(
      expect.objectContaining<Partial<ZonedDateTimeError>>({
        code: 'nonexistent_local_datetime',
      }),
    );
  });

  it('uses the earlier instant for repeated DST wall times', () => {
    expect(localDateTimeToInstant('2025-11-02T01:30', 'America/New_York')).toBe(
      '2025-11-02T05:30:00Z',
    );
  });

  it('builds half-open natural-day windows across DST and non-hour offsets', () => {
    expect(naturalDayWindow('2025-03-09', 'America/New_York')).toEqual({
      start: '2025-03-09T05:00:00Z',
      end: '2025-03-10T04:00:00Z',
    });
    expect(naturalDayWindow('2025-01-01', 'Asia/Kathmandu')).toEqual({
      start: '2024-12-31T18:15:00Z',
      end: '2025-01-01T18:15:00Z',
    });
  });

  it('derives and shifts report months without the browser timezone', () => {
    const now = Temporal.Instant.from('2024-12-31T18:30:00Z');
    expect(monthInTimeZone('Asia/Shanghai', now)).toBe('2025-01');
    expect(monthInTimeZone('America/New_York', now)).toBe('2024-12');
    expect(shiftReportMonth('2025-01', -2)).toBe('2024-11');
  });
});
