import type { components } from '../generated/api-types';
import { http } from './http';

export type Transfer = components['schemas']['Transfer'];
export type TransferPage = components['schemas']['TransferPage'];
export type CreateTransferRequest = components['schemas']['CreateTransferRequest'];

export interface TransferFilters {
  accountId?: number;
  from?: string;
  to?: string;
  pageSize?: number;
}

export async function listTransfers(
  filters: TransferFilters,
  cursor?: string,
  signal?: AbortSignal,
): Promise<TransferPage> {
  const response = await http.get<TransferPage>('/api/v1/transfers', {
    params: { ...filters, cursor },
    signal,
  });
  return response.data;
}

export async function getTransfer(
  transferGroupId: number,
  signal?: AbortSignal,
): Promise<Transfer> {
  const response = await http.get<Transfer>(`/api/v1/transfers/${transferGroupId}`, {
    signal,
  });
  return response.data;
}

export async function createTransfer(
  payload: CreateTransferRequest,
  idempotencyKey: string,
  signal?: AbortSignal,
): Promise<Transfer> {
  const response = await http.post<Transfer>('/api/v1/transfers', payload, {
    headers: { 'Idempotency-Key': idempotencyKey },
    signal,
  });
  return response.data;
}

export async function correctTransfer(
  transferGroupId: number,
  payload: CreateTransferRequest,
  idempotencyKey: string,
  signal?: AbortSignal,
): Promise<Transfer> {
  const response = await http.post<Transfer>(
    `/api/v1/transfers/${transferGroupId}/correction`,
    payload,
    { headers: { 'Idempotency-Key': idempotencyKey }, signal },
  );
  return response.data;
}

export async function deleteTransfer(transferGroupId: number, signal?: AbortSignal): Promise<void> {
  await http.delete(`/api/v1/transfers/${transferGroupId}`, { signal });
}
