import { createPinia, setActivePinia } from 'pinia';
import { HttpResponse, delay, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import type { Transfer } from '../services/transfer-api';
import { server } from '../test/server';
import { useTransferStore } from './transfers';
import { useUserContextStore } from './user-context';

function transfer(id: number): Transfer {
  return {
    transferGroupId: id,
    mode: 'BothAmounts',
    sourceAccountId: 1,
    targetAccountId: 2,
    outgoingTransactionId: id * 10,
    incomingTransactionId: id * 10 + 1,
    adjustmentTransactionIds: [],
    outgoingAmount: String(id),
    incomingAmount: String(id),
    sourceCurrencyCode: 'CNY',
    targetCurrencyCode: 'CNY',
    rate: null,
    feeAmount: null,
    feeSource: null,
    feeAccountId: null,
    feeCurrencyCode: null,
    description: `Transfer ${id}`,
    occurredAt: '2026-07-18T02:00:00Z',
    createdAt: '2026-07-18T02:00:01Z',
    deletedAt: null,
    correctsTransferGroupId: null,
    correctedByTransferGroupId: null,
  };
}

describe('transfer store', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('appends aggregate cursor pages without duplicate boundary rows', async () => {
    server.use(
      mockHttp.get('*/api/v1/transfers', ({ request }) => {
        const cursor = new URL(request.url).searchParams.get('cursor');
        return cursor
          ? HttpResponse.json({ items: [transfer(2), transfer(1)], nextCursor: null })
          : HttpResponse.json({ items: [transfer(3), transfer(2)], nextCursor: 'page-2' });
      }),
    );
    const store = useTransferStore();
    await expect(store.load({ pageSize: 2 })).resolves.toBe(true);
    await expect(store.loadMore()).resolves.toBe(true);
    expect(store.items.map(({ transferGroupId }) => transferGroupId)).toEqual([3, 2, 1]);
  });

  it('does not publish a late detail after session state is cleared', async () => {
    server.use(
      mockHttp.get('*/api/v1/transfers/7', async () => {
        await delay(70);
        return HttpResponse.json(transfer(7));
      }),
    );
    const store = useTransferStore();
    const pending = store.loadDetail(7);
    await delay(10);
    store.clear();
    await expect(pending).resolves.toBe(false);
    expect(store.selected).toBeNull();
    expect(store.detailState).toBe('idle');
  });

  it('replaces an aggregate correction and invalidates financial projections once', async () => {
    server.use(
      mockHttp.get('*/api/v1/transfers/7', () => HttpResponse.json(transfer(7))),
      mockHttp.post('*/api/v1/transfers/7/correction', () =>
        HttpResponse.json({ ...transfer(8), correctsTransferGroupId: 7 }, { status: 201 }),
      ),
    );
    const store = useTransferStore();
    const context = useUserContextStore();
    await store.loadDetail(7);
    await expect(
      store.correctSelected({
        sourceAccountId: 1,
        targetAccountId: 2,
        mode: 'BothAmounts',
        outgoingAmount: '8',
        incomingAmount: '8',
      }),
    ).resolves.toMatchObject({ transferGroupId: 8, correctsTransferGroupId: 7 });
    expect(store.selected?.transferGroupId).toBe(8);
    expect(context.aggregationRevision).toBe(1);
  });
});
