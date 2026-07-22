import axios, { type AxiosError } from 'axios';
import type { components } from '../generated/api-types';
import { translate, type MessageKey } from '../i18n';

export type ErrorResponse = components['schemas']['ErrorResponse'];

export interface ApiErrorShape {
  status: number;
  errorCode: string;
  message: string;
  traceId: string;
  fieldErrors: Record<string, string>;
  retryable: boolean;
}

const errorMessages: Record<string, MessageKey> = {
  VALIDATION_ERROR: 'error.validation',
  INVALID_INPUT: 'error.invalidInput',
  MISSING_REQUIRED_FIELD: 'error.missingField',
  INVALID_FORMAT: 'error.invalidFormat',
  UNAUTHORIZED: 'error.unauthorized',
  INVALID_TOKEN: 'error.unauthorized',
  EXPIRED_TOKEN: 'error.unauthorized',
  FORBIDDEN: 'error.forbidden',
  INSUFFICIENT_PERMISSIONS: 'error.forbidden',
  NOT_FOUND: 'error.notFound',
  USER_NOT_FOUND: 'error.notFound',
  ACCOUNT_NOT_FOUND: 'error.notFound',
  TRANSACTION_NOT_FOUND: 'error.notFound',
  CATEGORY_NOT_FOUND: 'error.notFound',
  CONFLICT: 'error.conflict',
  DUPLICATE_RESOURCE: 'error.conflict',
  VERSION_MISMATCH: 'error.versionConflict',
  OPTIMISTIC_LOCK_FAILURE: 'error.versionConflict',
  DOMAIN_RULE_VIOLATION: 'error.domainRule',
  INVALID_CURRENCY_OPERATION: 'error.invalidCurrency',
  INSUFFICIENT_BALANCE: 'error.insufficientBalance',
  TRANSFER_AMOUNT_MISMATCH: 'error.transferMismatch',
  INVALID_EXCHANGE_RATE: 'error.invalidRate',
  CROSS_CURRENCY_WITHOUT_RATE: 'error.rateRequired',
  CATEGORY_BOARD_MISMATCH: 'error.categoryBoard',
  ARCHIVED_ACCOUNT_OPERATION: 'error.archivedAccount',
  RESOURCE_LIMIT_EXCEEDED: 'error.resourceLimit',
  EXTERNAL_SERVICE_ERROR: 'error.externalService',
  INFRASTRUCTURE_FAILURE: 'error.internal',
  DATABASE_ERROR: 'error.internal',
  DATABASE_CONNECTION_FAILED: 'error.internal',
  TRANSACTION_FAILED: 'error.internal',
  CONFIGURATION_ERROR: 'error.internal',
  INTERNAL_ERROR: 'error.internal',
  UNEXPECTED_ERROR: 'error.internal',
  SERVICE_UNAVAILABLE: 'error.serviceUnavailable',
  PAYLOAD_TOO_LARGE: 'error.payloadTooLarge',
  RATE_LIMITED: 'error.rateLimited',
  NETWORK_ERROR: 'error.network',
};

const fieldErrorMessages: Record<string, MessageKey> = {
  invalid_version_etag: 'error.invalidVersionEtag',
};

function localizedMessage(errorCode: string, fallback?: string): string {
  const key = errorMessages[errorCode];
  return key ? translate(key) : fallback || translate('common.serviceUnavailable');
}

function projectAxiosError(error: AxiosError, payload?: Partial<ErrorResponse>): ApiErrorShape {
  const status = error.response?.status ?? 0;
  const errorCode =
    typeof payload?.error_code === 'string'
      ? payload.error_code
      : status === 0
        ? 'NETWORK_ERROR'
        : 'UNKNOWN_ERROR';
  const fields = Array.isArray(payload?.field_errors) ? payload.field_errors : [];
  const fieldErrors = Object.fromEntries(
    fields
      .filter((field) => typeof field?.field === 'string' && typeof field.message === 'string')
      .map((field) => [
        field.field,
        typeof field.code === 'string' && fieldErrorMessages[field.code]
          ? translate(fieldErrorMessages[field.code])
          : field.message,
      ]),
  );
  const responseTraceId = error.response?.headers['x-trace-id'];
  return {
    status,
    errorCode,
    message: localizedMessage(
      errorCode,
      typeof payload?.message === 'string' ? payload.message : undefined,
    ),
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
    message: translate('common.unexpectedError'),
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
