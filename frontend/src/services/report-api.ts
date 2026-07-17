import type { components, operations } from '../generated/api-types';
import { http } from './http';

export type ReportAnalysis =
  operations['getReportAnalysis']['responses'][200]['content']['application/json'];
export type ReportDimension = components['schemas']['ReportDimension'];

export interface ReportFilters {
  startDate: string;
  endDate: string;
  dimension: ReportDimension;
}

export interface TransactionExportFilters {
  from: string;
  to: string;
  accountId?: number;
  type?: 'income' | 'expense' | 'transfer' | 'adjustment';
  categoryId?: number;
  tagId?: number;
  keyword?: string;
}

export interface CsvDownload {
  blob: Blob;
  filename: string;
  rowCount: number;
}

export async function getReportAnalysis(
  filters: ReportFilters,
  signal?: AbortSignal,
): Promise<ReportAnalysis> {
  const response = await http.get<ReportAnalysis>('/api/v1/reports/analysis', {
    params: filters,
    signal,
  });
  return response.data;
}

function responseFilename(disposition: unknown): string {
  if (typeof disposition !== 'string') return 'transactions.csv';
  const match = disposition.match(/filename="([A-Za-z0-9._-]+)"/);
  return match?.[1] ?? 'transactions.csv';
}

export async function exportTransactionsCsv(
  filters: TransactionExportFilters,
  signal?: AbortSignal,
): Promise<CsvDownload> {
  const response = await http.get<Blob>('/api/v1/exports/transactions.csv', {
    params: filters,
    responseType: 'blob',
    signal,
  });
  const rawCount = Number(response.headers['x-export-row-count']);
  return {
    blob: response.data,
    filename: responseFilename(response.headers['content-disposition']),
    rowCount: Number.isSafeInteger(rawCount) && rawCount >= 0 ? rawCount : 0,
  };
}
