import type { components } from '../generated/api-types';
import { http } from './http';

export type OperationsSummary = components['schemas']['OperationsSummary'];
export type DeadLetterItem = components['schemas']['DeadLetterItem'];
export type DeadLetterPage = components['schemas']['DeadLetterPage'];
export type DeadLetterRetryResponse = components['schemas']['DeadLetterRetryResponse'];

export async function getOperationsSummary(signal?: AbortSignal): Promise<OperationsSummary> {
  const response = await http.get<OperationsSummary>('/api/v1/operations/summary', { signal });
  return response.data;
}

export async function getOperationsMetrics(signal?: AbortSignal): Promise<string> {
  const response = await http.get<string>('/api/v1/operations/metrics', {
    headers: { Accept: 'text/plain' },
    signal,
  });
  return response.data;
}

export async function listDeadLetters(
  cursor?: string,
  pageSize = 50,
  signal?: AbortSignal,
): Promise<DeadLetterPage> {
  const response = await http.get<DeadLetterPage>('/api/v1/operations/dead-letters', {
    params: { cursor, pageSize },
    signal,
  });
  return response.data;
}

export async function retryDeadLetter(
  outboxId: string,
  idempotencyKey: string,
  signal?: AbortSignal,
): Promise<DeadLetterRetryResponse> {
  const response = await http.post<DeadLetterRetryResponse>(
    `/api/v1/operations/dead-letters/${encodeURIComponent(outboxId)}/retry`,
    undefined,
    {
      headers: { 'Idempotency-Key': idempotencyKey },
      signal,
    },
  );
  return response.data;
}
