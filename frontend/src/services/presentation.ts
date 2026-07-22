import Decimal from 'decimal.js';
import type { components } from '../generated/api-types';

type NumberFormat = components['schemas']['NumberFormat'];

export interface PresentationLocale {
  locale: string;
  timeZone: string;
  dateFormat?: string;
}

export interface EnumOption<T extends string> {
  value: T;
  label: string;
}

export function formatDecimalString(
  value: string,
  locale = 'en-US',
  maximumFractionDigits = 8,
  numberFormat?: NumberFormat,
): string {
  try {
    const decimal = new Decimal(value).toDecimalPlaces(
      maximumFractionDigits,
      Decimal.ROUND_HALF_EVEN,
    );
    const normalized = decimal.toFixed(Math.min(decimal.decimalPlaces(), maximumFractionDigits));
    const unsigned = normalized.startsWith('-') ? normalized.slice(1) : normalized;
    const [whole, fraction] = unsigned.split('.');
    const parts = new Intl.NumberFormat(locale).formatToParts(1000.1);
    const separators: Record<NumberFormat, { group: string; decimal: string }> = {
      '1,234.56': { group: ',', decimal: '.' },
      '1.234,56': { group: '.', decimal: ',' },
      '1 234,56': { group: ' ', decimal: ',' },
    };
    const selected = numberFormat ? separators[numberFormat] : undefined;
    const group = selected?.group ?? parts.find((part) => part.type === 'group')?.value ?? ',';
    const decimalSeparator =
      selected?.decimal ?? parts.find((part) => part.type === 'decimal')?.value ?? '.';
    const grouped = whole.replace(/\B(?=(\d{3})+(?!\d))/g, group);
    const formatter = new Intl.NumberFormat(locale, { useGrouping: false });
    const digits = Array.from({ length: 10 }, (_, digit) => formatter.format(digit));
    const localizeDigits = (text: string) => text.replace(/\d/g, (digit) => digits[Number(digit)]);
    const minusSign =
      formatter.formatToParts(-1).find((part) => part.type === 'minusSign')?.value ?? '-';
    const sign = decimal.isNegative() && !decimal.isZero() ? minusSign : '';
    return `${sign}${localizeDigits(grouped)}${
      fraction ? `${decimalSeparator}${localizeDigits(fraction)}` : ''
    }`;
  } catch {
    return value;
  }
}

function formatPreferredDate(
  instant: Date,
  locale: string,
  timeZone: string,
  dateFormat: string | undefined,
): string | null {
  const pattern = dateFormat
    ?.toUpperCase()
    .match(/^(YYYY|MM|DD)([-/.])(YYYY|MM|DD)\2(YYYY|MM|DD)$/);
  if (!pattern || new Set([pattern[1], pattern[3], pattern[4]]).size !== 3) return null;

  const parts = new Intl.DateTimeFormat(locale, {
    timeZone,
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
  }).formatToParts(instant);
  const values = new Map(parts.map((part) => [part.type, part.value]));
  const tokens: Record<string, string | undefined> = {
    YYYY: values.get('year'),
    MM: values.get('month'),
    DD: values.get('day'),
  };
  if (!tokens.YYYY || !tokens.MM || !tokens.DD) return null;
  return [pattern[1], pattern[3], pattern[4]].map((token) => tokens[token]).join(pattern[2]);
}

export function formatInstant(
  value: string,
  { locale, timeZone, dateFormat }: PresentationLocale,
): string {
  const instant = new Date(value);
  if (Number.isNaN(instant.getTime())) return value;
  try {
    const preferredDate = formatPreferredDate(instant, locale, timeZone, dateFormat);
    if (preferredDate) {
      const time = new Intl.DateTimeFormat(locale, {
        timeZone,
        hour: 'numeric',
        minute: '2-digit',
        timeZoneName: 'shortOffset',
      }).format(instant);
      return `${preferredDate} ${time}`;
    }
    const date = new Intl.DateTimeFormat(locale, {
      timeZone,
      dateStyle: 'medium',
    }).format(instant);
    const time = new Intl.DateTimeFormat(locale, {
      timeZone,
      hour: 'numeric',
      minute: '2-digit',
      timeZoneName: 'shortOffset',
    }).format(instant);
    return `${date}, ${time}`;
  } catch {
    return value;
  }
}

export function toChartRatios(values: readonly string[]): number[] {
  const magnitudes = values.map((value) => {
    try {
      return new Decimal(value).abs();
    } catch {
      return new Decimal(0);
    }
  });
  const maximum = Decimal.max(new Decimal(1), ...magnitudes);
  return magnitudes.map((value) => value.div(maximum).toNumber());
}

export function toEnumOptions<T extends string>(
  values: readonly T[],
  labels: Readonly<Record<T, string>>,
): EnumOption<T>[] {
  return values.map((value) => ({ value, label: labels[value] }));
}
