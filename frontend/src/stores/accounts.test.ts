import { HttpResponse, delay, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import { createPinia, setActivePinia } from 'pinia';
import { server } from '../test/server';
import type { Account } from '../services/account-api';
import { useAccountStore } from './accounts';

function account(id: number, overrides: Partial<Account> = {}): Account {
  return {
    id,
    name: `Account ${id}`,
    type: 'cash',
    subtype: 'wallet',
    category: 'asset',
    currencyCode: 'CNY',
    description: '',
    isArchived: false,
    archivedAt: null,
    createdAt: '2026-07-16T00:00:00Z',
    updatedAt: '2026-07-16T00:00:00Z',
    version: 1,
    ...overrides,
  };
}

const balance = {
  accountId: 1,
  currencyCode: 'CNY',
  balance: '125.50000000',
  lastTransactionId: 8,
  updatedAt: '2026-07-16T01:00:00Z',
};

describe('account store', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('publishes account detail and balance atomically', async () => {
    server.use(
      mockHttp.get('*/api/v1/accounts/1', () =>
        HttpResponse.json(account(1), { headers: { ETag: '"1"' } }),
      ),
      mockHttp.get('*/api/v1/accounts/1/balance', async () => {
        await delay(60);
        return HttpResponse.json(balance);
      }),
    );
    const store = useAccountStore();
    const pending = store.loadDetail(1);
    await delay(10);

    expect(store.selected).toBeNull();
    expect(store.selectedBalance).toBeNull();
    await expect(pending).resolves.toBe(true);
    expect(store.selected?.id).toBe(1);
    expect(store.selectedBalance?.balance).toBe('125.50000000');
    expect(store.selectedEtag).toBe('"1"');
  });

  it('rejects a detail and balance pair from different account versions', async () => {
    server.use(
      mockHttp.get('*/api/v1/accounts/1', () =>
        HttpResponse.json(account(1), { headers: { ETag: '"1"' } }),
      ),
      mockHttp.get('*/api/v1/accounts/1/balance', () =>
        HttpResponse.json({ ...balance, currencyCode: 'USD' }),
      ),
    );
    const store = useAccountStore();

    await expect(store.loadDetail(1)).rejects.toThrow('inconsistent');
    expect(store.selected).toBeNull();
    expect(store.selectedBalance).toBeNull();
    expect(store.detailState).toBe('error');
  });

  it('keeps only the newest list response', async () => {
    let requestNumber = 0;
    server.use(
      mockHttp.get('*/api/v1/accounts', async () => {
        requestNumber += 1;
        const current = requestNumber;
        if (current === 1) await delay(70);
        return HttpResponse.json([account(current)]);
      }),
    );
    const store = useAccountStore();
    const first = store.loadList('active');
    await delay(10);
    const second = store.loadList('archived');

    await expect(second).resolves.toBe(true);
    await expect(first).resolves.toBe(false);
    expect(store.listStatus).toBe('archived');
    expect(store.items.map(({ id }) => id)).toEqual([2]);
  });

  it('does not let a late detail response overwrite a newer list', async () => {
    server.use(
      mockHttp.get('*/api/v1/accounts/1', async () => {
        await delay(70);
        return HttpResponse.json(account(1, { name: 'Stale detail' }), {
          headers: { ETag: '"1"' },
        });
      }),
      mockHttp.get('*/api/v1/accounts/1/balance', async () => {
        await delay(70);
        return HttpResponse.json(balance);
      }),
      mockHttp.get('*/api/v1/accounts', () =>
        HttpResponse.json([account(1, { name: 'Current list', version: 2 })]),
      ),
    );
    const store = useAccountStore();
    const detail = store.loadDetail(1);
    await delay(10);

    await expect(store.loadList('active')).resolves.toBe(true);
    await expect(detail).resolves.toBe(false);
    await delay(80);
    expect(store.items).toHaveLength(1);
    expect(store.items[0]?.name).toBe('Current list');
    expect(store.items[0]?.version).toBe(2);
    expect(store.selected).toBeNull();
  });

  it('rejects a late mutation after the current user is cleared', async () => {
    server.use(
      mockHttp.post('*/api/v1/accounts', async () => {
        await delay(70);
        return HttpResponse.json(account(9), { status: 201 });
      }),
    );
    const store = useAccountStore();
    const pending = store.create({
      name: 'Late account',
      type: 'cash',
      subtype: 'wallet',
      category: 'asset',
      currencyCode: 'CNY',
      description: '',
    });
    await delay(10);
    store.clear();

    await expect(pending).rejects.toBeDefined();
    await delay(80);
    expect(store.items).toEqual([]);
    expect(store.selected).toBeNull();
    expect(store.listState).toBe('idle');
  });

  it('reloads the authoritative version after archiving', async () => {
    let archived = false;
    server.use(
      mockHttp.get('*/api/v1/accounts/1', () =>
        HttpResponse.json(
          account(1, {
            isArchived: archived,
            archivedAt: archived ? '2026-07-16T02:00:00Z' : null,
            version: archived ? 2 : 1,
          }),
          { headers: { ETag: archived ? '"2"' : '"1"' } },
        ),
      ),
      mockHttp.get('*/api/v1/accounts/1/balance', () => HttpResponse.json(balance)),
      mockHttp.post('*/api/v1/accounts/1/archive', ({ request }) => {
        expect(request.headers.get('if-match')).toBe('"1"');
        archived = true;
        return new HttpResponse(null, { status: 204 });
      }),
    );
    const store = useAccountStore();
    await store.loadDetail(1);
    await store.archiveSelected();

    expect(store.selected?.isArchived).toBe(true);
    expect(store.selected?.version).toBe(2);
    expect(store.selectedEtag).toBe('"2"');
    expect(store.items).toEqual([]);
  });

  it('does not publish a late write after navigating to another account', async () => {
    server.use(
      mockHttp.get('*/api/v1/accounts/:accountId', ({ params }) => {
        const id = Number(params.accountId);
        return HttpResponse.json(account(id), { headers: { ETag: '"1"' } });
      }),
      mockHttp.get('*/api/v1/accounts/:accountId/balance', ({ params }) => {
        const id = Number(params.accountId);
        return HttpResponse.json({ ...balance, accountId: id });
      }),
      mockHttp.put('*/api/v1/accounts/1', async () => {
        await delay(70);
        return HttpResponse.json(account(1, { name: 'Late update', version: 2 }), {
          headers: { ETag: '"2"' },
        });
      }),
    );
    const store = useAccountStore();
    await store.loadDetail(1);
    const pending = store.updateSelected({
      name: 'Late update',
      type: 'cash',
      subtype: 'wallet',
      category: 'asset',
      currencyCode: 'CNY',
      description: '',
    });
    await delay(10);
    await expect(store.loadDetail(2)).resolves.toBe(true);

    await expect(pending).rejects.toBeDefined();
    await delay(80);
    expect(store.selected?.id).toBe(2);
    expect(store.selected?.name).toBe('Account 2');
  });
});
