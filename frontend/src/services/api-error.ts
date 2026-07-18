import axios, { type AxiosError } from 'axios';
import type { components } from '../generated/api-types';

export type ErrorResponse = components['schemas']['ErrorResponse'];

export interface ApiErrorShape {
  status: number;
  errorCode: string;
  message: string;
  traceId: string;
  fieldErrors: Record<string, string>;
  retryable: boolean;
}

function projectAxiosError(error: AxiosError, payload?: Partial<ErrorResponse>): ApiErrorShape {
  const status = error.response?.status ?? 0;
  const fields = Array.isArray(payload?.field_errors) ? payload.field_errors : [];
  const fieldErrors = Object.fromEntries(
    fields
      .filter((field) => typeof field?.field === 'string' && typeof field.message === 'string')
      .map((field) => [field.field, field.message]),
  );
  const responseTraceId = error.response?.headers['x-trace-id'];
  return {
    status,
    errorCode:
      typeof payload?.error_code === 'string'
        ? payload.error_code
        : status === 0
          ? 'NETWORK_ERROR'
          : 'UNKNOWN_ERROR',
    message:
      typeof payload?.message === 'string'
        ? payload.message
        : 'The service is unavailable right now.',
    traceId:
      typeof payload?.trace_id === 'string'
        ? payload.trace_id
        : typeof responseTraceId === 'string'
          ? responseTraceId
          : '',
    fieldErrors,
    retryable:
      typeof payload?.retryable === 'boolean' ? payload.retryable : status === 0 || status >= 500,
  };
}

function objectPayload(value: unknown): Partial<ErrorResponse> | undefined {
  return value !== null && typeof value === 'object' && !(value instanceof Blob)
    ? (value as Partial<ErrorResponse>)
    : undefined;
}

async function readBlob(blob: Blob): Promise<string> {
  if (typeof blob.text === 'function') return blob.text();
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.addEventListener('load', () => resolve(String(reader.result ?? '')));
    reader.addEventListener('error', () => reject(reader.error ?? new Error('Blob read failed')));
    reader.readAsText(blob);
  });
}

export function toApiError(error: unknown): ApiErrorShape {
  if (axios.isAxiosError(error)) {
    return projectAxiosError(error, objectPayload(error.response?.data));
  }
  return {
    status: 0,
    errorCode: 'UNKNOWN_ERROR',
    message: 'Something unexpected happened.',
    traceId: '',
    fieldErrors: {},
    retryable: false,
  };
}

export async function toApiErrorAsync(error: unknown): Promise<ApiErrorShape> {
  if (!axios.isAxiosError(error)) return toApiError(error);
  const data = error.response?.data;
  if (data instanceof Blob && data.size <= 65_536) {
    const contentType = String(error.response?.headers['content-type'] ?? data.type).toLowerCase();
    if (contentType.includes('json')) {
      try {
        return projectAxiosError(error, objectPayload(JSON.parse(await readBlob(data))));
      } catch {
        // Invalid proxy/error payloads use the same controlled fallback below.
      }
    }
  }
  return projectAxiosError(error, objectPayload(data));
}

export class ApiError extends Error {
  readonly details: ApiErrorShape;

  constructor(details: ApiErrorShape) {
    super(details.message);
    this.name = 'ApiError';
    this.details = details;
  }
}
