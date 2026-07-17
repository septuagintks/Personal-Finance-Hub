import type { components } from '../generated/api-types';
import { http } from './http';

export type Transaction = components['schemas']['Transaction'];
export type TransactionPage = components['schemas']['TransactionPage'];
export type CreateTransactionRequest = components['schemas']['CreateTransactionRequest'];
export type TransactionType = components['schemas']['TransactionType'];

export interface TransactionFilters {
  accountId?: number;
  type?: TransactionType;
  categoryId?: number;
  tagId?: number;
  from?: string;
  to?: string;
  keyword?: string;
  pageSize?: number;
}

export async function listTransactions(
  filters: TransactionFilters,
  cursor?: string,
  signal?: AbortSignal,
): Promise<TransactionPage> {
  const response = await http.get<TransactionPage>('/api/v1/transactions', {
    params: { ...filters, cursor },
    signal,
  });
  return response.data;
}

export async function getTransaction(
  transactionId: number,
  signal?: AbortSignal,
): Promise<Transaction> {
  const response = await http.get<Transaction>(`/api/v1/transactions/${transactionId}`, {
    signal,
  });
  return response.data;
}

export async function createTransaction(
  payload: CreateTransactionRequest,
  idempotencyKey: string,
  signal?: AbortSignal,
): Promise<Transaction> {
  const response = await http.post<Transaction>('/api/v1/transactions', payload, {
    headers: { 'Idempotency-Key': idempotencyKey },
    signal,
  });
  return response.data;
}

export async function correctTransaction(
  transactionId: number,
  payload: CreateTransactionRequest,
  idempotencyKey: string,
  signal?: AbortSignal,
): Promise<Transaction> {
  const response = await http.post<Transaction>(
    `/api/v1/transactions/${transactionId}/correction`,
    payload,
    {
      headers: { 'Idempotency-Key': idempotencyKey },
      signal,
    },
  );
  return response.data;
}

export async function deleteTransaction(
  transactionId: number,
  signal?: AbortSignal,
): Promise<void> {
  await http.delete(`/api/v1/transactions/${transactionId}`, { signal });
}
