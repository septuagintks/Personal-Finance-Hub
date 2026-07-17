import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import {
  getOperationsMetrics,
  getOperationsSummary,
  listDeadLetters,
  retryDeadLetter,
} from './operations-api';

const summary = {
  generatedAt: '2026-07-18T00:00:00Z',
  outbox: { pending: 1, processing: 0, published: 8, failed: 2, deadLetter: 1 },
  handlerReceipts: { count: 8, latestAt: '2026-07-18T00:00:00Z' },
  expiredIdempotency: 3,
  leases: [],
  jobs: [],
};

describe('operations API contract', () => {
  it('reads the bounded summary and Prometheus representation', async () => {
    server.use(
      mockHttp.get('*/api/v1/operations/summary', () => HttpResponse.json(summary)),
      mockHttp.get('*/api/v1/operations/metrics', ({ request }) => {
        expect(request.headers.get('accept')).toBe('text/plain');
        return new HttpResponse('pfh_outbox_messages{status="pending"} 1\n', {
          headers: { 'Content-Type': 'text/plain' },
        });
      }),
    );

    await expect(getOperationsSummary()).resolves.toEqual(summary);
    await expect(getOperationsMetrics()).resolves.toContain('pfh_outbox_messages');
  });

  it('paginates sanitized dead letters and retries with one intent key', async () => {
    let received = new URL('https://example.invalid');
    let retryKey = '';
    server.use(
      mockHttp.get('*/api/v1/operations/dead-letters', ({ request }) => {
        received = new URL(request.url);
        return HttpResponse.json({ items: [], nextCursor: null });
      }),
      mockHttp.post('*/api/v1/operations/dead-letters/dead-letter-1/retry', ({ request }) => {
        retryKey = request.headers.get('idempotency-key') ?? '';
        return HttpResponse.json(
          { outboxId: 'dead-letter-1', replayed: false, status: 'retry_scheduled' },
          { status: 202 },
        );
      }),
    );

    await listDeadLetters('opaque-cursor', 25);
    const result = await retryDeadLetter('dead-letter-1');

    expect(received.searchParams.get('cursor')).toBe('opaque-cursor');
    expect(received.searchParams.get('pageSize')).toBe('25');
    expect(retryKey).toMatch(/^dead-letter-\S+$/);
    expect(result.status).toBe('retry_scheduled');
  });
});
