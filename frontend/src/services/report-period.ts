import { shiftReportMonth } from './zoned-date-time';

export type DefaultReportPeriod =
  'current_month' | 'last_month' | 'last_3_months' | 'current_year' | 'custom';

export interface ReportPeriodPreference {
  defaultReportPeriod: DefaultReportPeriod;
  customReportStartMonth: string | null;
  customReportEndMonth: string | null;
}

export interface ReportMonthRange {
  startDate: string;
  endDate: string;
}

export function isReportMonth(value: string): boolean {
  return /^\d{4}-(0[1-9]|1[0-2])$/.test(value);
}

export function preferredReportRange(
  preference: ReportPeriodPreference | null | undefined,
  currentMonth: string,
): ReportMonthRange {
  switch (preference?.defaultReportPeriod) {
    case 'last_month': {
      const month = shiftReportMonth(currentMonth, -1);
      return { startDate: month, endDate: month };
    }
    case 'last_3_months':
      return { startDate: shiftReportMonth(currentMonth, -2), endDate: currentMonth };
    case 'current_year':
      return { startDate: `${currentMonth.slice(0, 4)}-01`, endDate: currentMonth };
    case 'custom': {
      const start = preference.customReportStartMonth;
      const end = preference.customReportEndMonth;
      if (start && end && isReportMonth(start) && isReportMonth(end) && start <= end) {
        return { startDate: start, endDate: end };
      }
      return { startDate: currentMonth, endDate: currentMonth };
    }
    case 'current_month':
    default:
      return { startDate: currentMonth, endDate: currentMonth };
  }
}

export function resolveReportRange(
  query: { startDate?: unknown; endDate?: unknown },
  preference: ReportPeriodPreference | null | undefined,
  currentMonth: string,
): ReportMonthRange {
  const start = typeof query.startDate === 'string' ? query.startDate : '';
  const end = typeof query.endDate === 'string' ? query.endDate : '';
  if (isReportMonth(start) && isReportMonth(end) && start <= end) {
    return { startDate: start, endDate: end };
  }
  return preferredReportRange(preference, currentMonth);
}
