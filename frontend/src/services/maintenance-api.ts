import type { components } from '../generated/api-types';
import { http } from './http';

export type UserAuditAction = components['schemas']['UserAuditAction'];
export type UserAuditLogItem = components['schemas']['UserAuditLogItem'];
export type UserAuditLogPage = components['schemas']['UserAuditLogPage'];
export type BalanceCacheRebuildItem = components['schemas']['BalanceCacheRebuildItem'];
export type BalanceCacheRebuildResponse = components['schemas']['BalanceCacheRebuildResponse'];

export interface AuditLogFilters {
  action?: UserAuditAction;
  resourceType?: string;
  from?: string;
  to?: string;
  pageSize?: number;
}

export async function listUserAuditLogs(
  filters: AuditLogFilters,
  cursor?: string,
  signal?: AbortSignal,
): Promise<UserAuditLogPage> {
  const response = await http.get<UserAuditLogPage>('/api/v1/maintenance/audit-logs', {
    params: { ...filters, cursor },
    signal,
  });
  return response.data;
}

export async function rebuildAllBalanceCaches(
  signal?: AbortSignal,
): Promise<BalanceCacheRebuildResponse> {
  const response = await http.post<BalanceCacheRebuildResponse>(
    '/api/v1/maintenance/accounts/balance-cache/rebuild',
    undefined,
    { _pfhReplayAfterRefresh: true, signal },
  );
  return response.data;
}

export async function rebuildAccountBalanceCache(
  accountId: number,
  signal?: AbortSignal,
): Promise<BalanceCacheRebuildResponse> {
  const response = await http.post<BalanceCacheRebuildResponse>(
    `/api/v1/maintenance/accounts/${accountId}/balance-cache/rebuild`,
    undefined,
    { _pfhReplayAfterRefresh: true, signal },
  );
  return response.data;
}
