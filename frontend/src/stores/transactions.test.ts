import { createPinia, setActivePinia } from 'pinia';
import { HttpResponse, delay, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import type { Transaction } from '../services/transaction-api';
import { server } from '../test/server';
import { useTransactionStore } from './transactions';
import { useUserContextStore } from './user-context';

function transaction(id: number): Transaction {
  return {
    id,
    accountId: 4,
    type: 'expense',
    amount: String(id),
    currencyCode: 'CNY',
    categoryId: null,
    categoryName: null,
    categoryDeleted: false,
    tags: [],
    transferGroupId: null,
    correctsTransactionId: null,
    correctedByTransactionId: null,
    description: `Transaction ${id}`,
    occurredAt: '2026-07-16T04:00:00Z',
    createdAt: '2026-07-16T04:01:00Z',
    deletedAt: null,
  };
}

describe('transaction store', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('appends cursor pages without duplicating a repeated boundary row', async () => {
    server.use(
      mockHttp.get('*/api/v1/transactions', ({ request }) => {
        const cursor = new URL(request.url).searchParams.get('cursor');
        return cursor
          ? HttpResponse.json({ items: [transaction(2), transaction(1)], nextCursor: null })
          : HttpResponse.json({ items: [transaction(3), transaction(2)], nextCursor: 'page-2' });
      }),
    );
    const store = useTransactionStore();

    await expect(store.load({ pageSize: 2 })).resolves.toBe(true);
    await expect(store.loadMore()).resolves.toBe(true);
    expect(store.items.map(({ id }) => id)).toEqual([3, 2, 1]);
    expect(store.nextCursor).toBeNull();
  });

  it('does not publish a late detail after the session is cleared', async () => {
    server.use(
      mockHttp.get('*/api/v1/transactions/7', async () => {
        await delay(70);
        return HttpResponse.json(transaction(7));
      }),
    );
    const store = useTransactionStore();
    const pending = store.loadDetail(7);
    await delay(10);
    store.clear();

    await expect(pending).resolves.toBe(false);
    expect(store.selected).toBeNull();
    expect(store.detailState).toBe('idle');
  });

  it('replaces a corrected selection and invalidates financial aggregates once', async () => {
    server.use(
      mockHttp.get('*/api/v1/transactions/7', () => HttpResponse.json(transaction(7))),
      mockHttp.post('*/api/v1/transactions/7/correction', () =>
        HttpResponse.json({ ...transaction(8), correctsTransactionId: 7 }, { status: 201 }),
      ),
    );
    const store = useTransactionStore();
    const context = useUserContextStore();
    await store.loadDetail(7);

    await expect(
      store.correctSelected({
        accountId: 4,
        type: 'expense',
        amount: '8',
        currencyCode: 'CNY',
        tagIds: [],
      }),
    ).resolves.toMatchObject({ id: 8, correctsTransactionId: 7 });
    expect(store.selected?.id).toBe(8);
    expect(context.aggregationRevision).toBe(1);
  });
});
