import axios from 'axios';
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

export function toApiError(error: unknown): ApiErrorShape {
  if (axios.isAxiosError(error)) {
    const status = error.response?.status ?? 0;
    const payload = error.response?.data as Partial<ErrorResponse> | undefined;
    const fieldErrors = Object.fromEntries(
      (payload?.field_errors ?? []).map((field) => [field.field, field.message]),
    );
    return {
      status,
      errorCode: payload?.error_code ?? (status === 0 ? 'NETWORK_ERROR' : 'UNKNOWN_ERROR'),
      message: payload?.message ?? 'The service is unavailable right now.',
      traceId: payload?.trace_id ?? error.response?.headers['x-trace-id'] ?? '',
      fieldErrors,
      retryable: payload?.retryable ?? (status === 0 || status >= 500),
    };
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

export class ApiError extends Error {
  readonly details: ApiErrorShape;

  constructor(details: ApiErrorShape) {
    super(details.message);
    this.name = 'ApiError';
    this.details = details;
  }
}
