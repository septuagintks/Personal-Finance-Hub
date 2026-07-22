import { Temporal } from '@js-temporal/polyfill';

export type ZonedDateTimeErrorCode =
  | 'invalid_instant'
  | 'invalid_local_datetime'
  | 'nonexistent_local_datetime'
  | 'invalid_date'
  | 'invalid_timezone';

export class ZonedDateTimeError extends Error {
  constructor(readonly code: ZonedDateTimeErrorCode) {
    super(code);
    this.name = 'ZonedDateTimeError';
  }
}

function timezoneError(error: unknown, fallback: ZonedDateTimeErrorCode): never {
  if (error instanceof ZonedDateTimeError) throw error;
  if (error instanceof RangeError && /time zone/i.test(error.message)) {
    throw new ZonedDateTimeError('invalid_timezone');
  }
  throw new ZonedDateTimeError(fallback);
}

export function instantToLocalDateTime(value: string, timeZone: string): string {
  try {
    return Temporal.Instant.from(value)
      .toZonedDateTimeISO(timeZone)
      .toPlainDateTime()
      .toString({ smallestUnit: 'minute' });
  } catch (error) {
    return timezoneError(error, 'invalid_instant');
  }
}

export function localDateTimeToInstant(value: string, timeZone: string): string {
  try {
    const local = Temporal.PlainDateTime.from(value);
    const earlier = local.toZonedDateTime(timeZone, { disambiguation: 'earlier' });
    const later = local.toZonedDateTime(timeZone, { disambiguation: 'later' });
    if (!earlier.toPlainDateTime().equals(local) || !later.toPlainDateTime().equals(local)) {
      throw new ZonedDateTimeError('nonexistent_local_datetime');
    }
    return earlier.toInstant().toString();
  } catch (error) {
    return timezoneError(error, 'invalid_local_datetime');
  }
}

function startOfNaturalDay(date: Temporal.PlainDate, timeZone: string): Temporal.Instant {
  const zoned = date.toZonedDateTime({ timeZone, plainTime: '00:00' });
  if (!zoned.toPlainDate().equals(date)) {
    throw new ZonedDateTimeError('invalid_date');
  }
  return zoned.toInstant();
}

export function naturalDayWindow(value: string, timeZone: string): { start: string; end: string } {
  try {
    const date = Temporal.PlainDate.from(value);
    return {
      start: startOfNaturalDay(date, timeZone).toString(),
      end: startOfNaturalDay(date.add({ days: 1 }), timeZone).toString(),
    };
  } catch (error) {
    return timezoneError(error, 'invalid_date');
  }
}

export function monthInTimeZone(
  timeZone: string,
  now: Temporal.Instant = Temporal.Now.instant(),
): string {
  try {
    const date = now.toZonedDateTimeISO(timeZone);
    return `${String(date.year).padStart(4, '0')}-${String(date.month).padStart(2, '0')}`;
  } catch (error) {
    return timezoneError(error, 'invalid_timezone');
  }
}

export function shiftReportMonth(value: string, months: number): string {
  try {
    return Temporal.PlainYearMonth.from(value).add({ months }).toString();
  } catch {
    throw new ZonedDateTimeError('invalid_date');
  }
}
