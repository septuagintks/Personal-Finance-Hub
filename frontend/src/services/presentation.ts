import Decimal from 'decimal.js';
import type { components } from '../generated/api-types';

type NumberFormat = components['schemas']['NumberFormat'];

export const PRESENTATION_FORMATTER_CACHE_LIMITS = {
  number: 16,
  dateTime: 64,
} as const;

class BoundedFormatterCache<T> {
  private readonly values = new Map<string, T>();

  constructor(private readonly limit: number) {}

  getOrCreate(key: string, create: () => T): T {
    const existing = this.values.get(key);
    if (existing !== undefined) {
      this.values.delete(key);
      this.values.set(key, existing);
      return existing;
    }

    const value = create();
    this.values.set(key, value);
    while (this.values.size > this.limit) {
      const oldest = this.values.keys().next().value;
      if (oldest === undefined) break;
      this.values.delete(oldest);
    }
    return value;
  }

  clear(): void {
    this.values.clear();
  }

  get size(): number {
    return this.values.size;
  }
}

interface NumberSymbols {
  group: string;
  decimal: string;
  digits: string[];
  minusSign: string;
}

const numberSymbolsCache = new BoundedFormatterCache<NumberSymbols>(
  PRESENTATION_FORMATTER_CACHE_LIMITS.number,
);
const dateTimeFormatterCache = new BoundedFormatterCache<Intl.DateTimeFormat>(
  PRESENTATION_FORMATTER_CACHE_LIMITS.dateTime,
);

function numberSymbols(locale: string): NumberSymbols {
  return numberSymbolsCache.getOrCreate(locale, () => {
    const formatter = new Intl.NumberFormat(locale);
    const parts = formatter.formatToParts(1000.1);
    return {
      group: parts.find((part) => part.type === 'group')?.value ?? ',',
      decimal: parts.find((part) => part.type === 'decimal')?.value ?? '.',
      digits: Array.from({ length: 10 }, (_, digit) => formatter.format(digit)),
      minusSign:
        formatter.formatToParts(-1).find((part) => part.type === 'minusSign')?.value ?? '-',
    };
  });
}

function dateTimeFormatter(
  locale: string,
  timeZone: string,
  mode: 'date-parts' | 'date-medium' | 'time-offset',
): Intl.DateTimeFormat {
  const key = JSON.stringify([locale, timeZone, mode]);
  return dateTimeFormatterCache.getOrCreate(key, () => {
    if (mode === 'date-parts') {
      return new Intl.DateTimeFormat(locale, {
        timeZone,
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
      });
    }
    if (mode === 'date-medium') {
      return new Intl.DateTimeFormat(locale, { timeZone, dateStyle: 'medium' });
    }
    return new Intl.DateTimeFormat(locale, {
      timeZone,
      hour: 'numeric',
      minute: '2-digit',
      timeZoneName: 'shortOffset',
    });
  });
}

export function clearPresentationFormatterCaches(): void {
  numberSymbolsCache.clear();
  dateTimeFormatterCache.clear();
}

export function presentationFormatterCacheSizes(): { number: number; dateTime: number } {
  return { number: numberSymbolsCache.size, dateTime: dateTimeFormatterCache.size };
}

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
    const localeSymbols = numberSymbols(locale);
    const separators: Record<NumberFormat, { group: string; decimal: string }> = {
      '1,234.56': { group: ',', decimal: '.' },
      '1.234,56': { group: '.', decimal: ',' },
      '1 234,56': { group: ' ', decimal: ',' },
    };
    const selected = numberFormat ? separators[numberFormat] : undefined;
    const group = selected?.group ?? localeSymbols.group;
    const decimalSeparator = selected?.decimal ?? localeSymbols.decimal;
    const grouped = whole.replace(/\B(?=(\d{3})+(?!\d))/g, group);
    const localizeDigits = (text: string) =>
      text.replace(/\d/g, (digit) => localeSymbols.digits[Number(digit)] ?? digit);
    const sign = decimal.isNegative() && !decimal.isZero() ? localeSymbols.minusSign : '';
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

  const parts = dateTimeFormatter(locale, timeZone, 'date-parts').formatToParts(instant);
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
      const time = dateTimeFormatter(locale, timeZone, 'time-offset').format(instant);
      return `${preferredDate} ${time}`;
    }
    const date = dateTimeFormatter(locale, timeZone, 'date-medium').format(instant);
    const time = dateTimeFormatter(locale, timeZone, 'time-offset').format(instant);
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
