import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import {
  correctTransaction,
  createTransaction,
  listTransactions,
  type CreateTransactionRequest,
  type Transaction,
} from './transaction-api';

const transaction: Transaction = {
  id: 17,
  accountId: 4,
  type: 'expense',
  amount: '12.5',
  currencyCode: 'CNY',
  categoryId: null,
  categoryName: null,
  categoryDeleted: false,
  tags: [],
  transferGroupId: null,
  correctsTransactionId: null,
  correctedByTransactionId: null,
  description: 'Lunch',
  occurredAt: '2026-07-16T04:00:00Z',
  createdAt: '2026-07-16T04:01:00Z',
  deletedAt: null,
};

const payload: CreateTransactionRequest = {
  accountId: 4,
  type: 'expense',
  amount: '12.50',
  currencyCode: 'CNY',
  categoryId: null,
  description: 'Lunch',
  occurredAt: '2026-07-16T04:00:00Z',
  tagIds: [],
};

describe('transaction API contract', () => {
  it('passes filters and the opaque cursor without interpreting it', async () => {
    let received = new URL('https://example.invalid');
    server.use(
      mockHttp.get('*/api/v1/transactions', ({ request }) => {
        received = new URL(request.url);
        return HttpResponse.json({ items: [transaction], nextCursor: 'next-page' });
      }),
    );

    await expect(
      listTransactions(
        { accountId: 4, type: 'expense', keyword: 'Lunch', pageSize: 25 },
        'opaque-cursor',
      ),
    ).resolves.toEqual({ items: [transaction], nextCursor: 'next-page' });
    expect(received.searchParams.get('accountId')).toBe('4');
    expect(received.searchParams.get('type')).toBe('expense');
    expect(received.searchParams.get('keyword')).toBe('Lunch');
    expect(received.searchParams.get('pageSize')).toBe('25');
    expect(received.searchParams.get('cursor')).toBe('opaque-cursor');
  });

  it('keeps the caller intent key on create and correction', async () => {
    const keys: string[] = [];
    server.use(
      mockHttp.post('*/api/v1/transactions', async ({ request }) => {
        keys.push(request.headers.get('idempotency-key') ?? '');
        expect(await request.json()).toEqual(payload);
        return HttpResponse.json(transaction, { status: 201 });
      }),
      mockHttp.post('*/api/v1/transactions/17/correction', async ({ request }) => {
        keys.push(request.headers.get('idempotency-key') ?? '');
        expect(await request.json()).toEqual(payload);
        return HttpResponse.json(
          { ...transaction, id: 18, correctsTransactionId: 17 },
          { status: 201 },
        );
      }),
    );

    await createTransaction(payload, 'create-intent');
    await correctTransaction(17, payload, 'correction-intent');
    expect(keys).toEqual(['create-intent', 'correction-intent']);
  });
});
