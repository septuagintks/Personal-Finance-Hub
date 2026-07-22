import axios from 'axios';
import { describe, expect, it } from 'vitest';
import { ApiError, toApiError, toApiErrorAsync } from './api-error';

describe('API error projection', () => {
  it('keeps controlled fields and retry semantics', () => {
    const projected = toApiError({
      response: {
        status: 409,
        data: {
          error_code: 'CONFLICT',
          message: 'The request conflicts with a newer fact.',
          trace_id: 'trace-409',
          retryable: false,
          field_errors: [{ field: 'amount', code: 'invalid', message: 'Invalid amount.' }],
        },
        headers: {},
      },
      isAxiosError: true,
    });
    expect(projected).toEqual({
      status: 409,
      errorCode: 'CONFLICT',
      message: 'This resource changed or already exists.',
      traceId: 'trace-409',
      fieldErrors: { amount: 'Invalid amount.' },
      retryable: false,
    });
  });

  it('does not expose unknown exception text', () => {
    const error = new ApiError(toApiError(new Error('database password')));
    expect(error.details.message).not.toContain('database password');
    expect(error.details.errorCode).toBe('UNKNOWN_ERROR');
  });

  it('recognizes Axios network failures as retryable', () => {
    const error = new axios.AxiosError('offline');
    const projected = toApiError(error);
    expect(projected.status).toBe(0);
    expect(projected.retryable).toBe(true);
  });

  it('decodes controlled JSON errors returned through a Blob response', async () => {
    const projected = await toApiErrorAsync({
      response: {
        status: 400,
        data: new Blob(
          [
            JSON.stringify({
              error_code: 'VALIDATION_ERROR',
              message: 'Narrow the export range.',
              trace_id: 'trace-export',
              retryable: false,
              field_errors: [],
            }),
          ],
          { type: 'application/json' },
        ),
        headers: { 'content-type': 'application/json; charset=utf-8' },
      },
      isAxiosError: true,
    });

    expect(projected).toMatchObject({
      status: 400,
      errorCode: 'VALIDATION_ERROR',
      message: 'Some request fields are invalid.',
      traceId: 'trace-export',
      retryable: false,
    });
  });
});
