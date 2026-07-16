import Decimal from 'decimal.js';

export interface PresentationLocale {
  locale: string;
  timeZone: string;
}

export interface EnumOption<T extends string> {
  value: T;
  label: string;
}

export function formatDecimalString(
  value: string,
  locale = 'en-US',
  maximumFractionDigits = 8,
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
    const group = parts.find((part) => part.type === 'group')?.value ?? ',';
    const decimalSeparator = parts.find((part) => part.type === 'decimal')?.value ?? '.';
    const grouped = whole.replace(/\B(?=(\d{3})+(?!\d))/g, group);
    const sign = decimal.isNegative() && !decimal.isZero() ? '-' : '';
    return `${sign}${grouped}${fraction ? `${decimalSeparator}${fraction}` : ''}`;
  } catch {
    return value;
  }
}

export function formatInstant(value: string, { locale, timeZone }: PresentationLocale): string {
  const instant = new Date(value);
  if (Number.isNaN(instant.getTime())) return value;
  return new Intl.DateTimeFormat(locale, {
    timeZone,
    dateStyle: 'medium',
    timeStyle: 'short',
  }).format(instant);
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
