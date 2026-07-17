import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import {
  listUserAuditLogs,
  rebuildAccountBalanceCache,
  rebuildAllBalanceCaches,
} from './maintenance-api';

const rebuildResponse = {
  accounts: [
    {
      accountId: 7,
      currencyCode: 'CNY',
      balance: '120.00000000',
      lastTransactionId: 9,
      sourceVersion: 4,
      cacheVersion: 3,
      rebuiltAt: '2026-07-18T00:00:00Z',
    },
  ],
};

describe('maintenance API contract', () => {
  it('sends only approved audit filters and preserves the opaque cursor', async () => {
    let received = new URL('https://example.invalid');
    server.use(
      mockHttp.get('*/api/v1/maintenance/audit-logs', ({ request }) => {
        received = new URL(request.url);
        return HttpResponse.json({ items: [], nextCursor: null });
      }),
    );

    await listUserAuditLogs(
      {
        action: 'update',
        resourceType: 'account',
        from: '2026-07-01T00:00:00Z',
        to: '2026-08-01T00:00:00Z',
        pageSize: 25,
      },
      'audit-cursor',
    );

    expect(Object.fromEntries(received.searchParams)).toEqual({
      action: 'update',
      resourceType: 'account',
      from: '2026-07-01T00:00:00Z',
      to: '2026-08-01T00:00:00Z',
      pageSize: '25',
      cursor: 'audit-cursor',
    });
  });

  it('exposes single-account and current-user rebuild commands', async () => {
    const paths: string[] = [];
    server.use(
      mockHttp.post('*/api/v1/maintenance/accounts/balance-cache/rebuild', ({ request }) => {
        paths.push(new URL(request.url).pathname);
        return HttpResponse.json(rebuildResponse);
      }),
      mockHttp.post('*/api/v1/maintenance/accounts/7/balance-cache/rebuild', ({ request }) => {
        paths.push(new URL(request.url).pathname);
        return HttpResponse.json(rebuildResponse);
      }),
    );

    await expect(rebuildAllBalanceCaches()).resolves.toEqual(rebuildResponse);
    await expect(rebuildAccountBalanceCache(7)).resolves.toEqual(rebuildResponse);
    expect(paths).toEqual([
      '/api/v1/maintenance/accounts/balance-cache/rebuild',
      '/api/v1/maintenance/accounts/7/balance-cache/rebuild',
    ]);
  });
});
