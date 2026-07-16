import type { operations } from '../generated/api-types';
import { http } from './http';

export type DashboardSummary =
  operations['getDashboardSummary']['responses'][200]['content']['application/json'];

export async function getDashboardSummary(signal?: AbortSignal): Promise<DashboardSummary> {
  const response = await http.get<DashboardSummary>('/api/v1/reports/dashboard-summary', {
    signal,
  });
  return response.data;
}
