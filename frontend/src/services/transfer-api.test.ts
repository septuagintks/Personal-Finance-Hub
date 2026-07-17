import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import {
  correctTransfer,
  createTransfer,
  listTransfers,
  type CreateTransferRequest,
  type Transfer,
} from './transfer-api';

const transfer: Transfer = {
  transferGroupId: 9,
  mode: 'BothAmounts',
  sourceAccountId: 1,
  targetAccountId: 2,
  outgoingTransactionId: 30,
  incomingTransactionId: 31,
  adjustmentTransactionIds: [32],
  outgoingAmount: '70',
  incomingAmount: '10',
  sourceCurrencyCode: 'CNY',
  targetCurrencyCode: 'USD',
  rate: '0.1428571429',
  feeAmount: '1',
  feeSource: 'SourceAccount',
  feeAccountId: 1,
  feeCurrencyCode: 'CNY',
  description: 'Settlement',
  occurredAt: '2026-07-18T02:00:00Z',
  createdAt: '2026-07-18T02:00:01Z',
  deletedAt: null,
  correctsTransferGroupId: null,
  correctedByTransferGroupId: null,
};

const payload: CreateTransferRequest = {
  sourceAccountId: 1,
  targetAccountId: 2,
  mode: 'BothAmounts',
  outgoingAmount: '70',
  incomingAmount: '10',
  rate: null,
  feeAmount: '1',
  feeSource: 'SourceAccount',
  feeAccountId: null,
  description: 'Settlement',
  occurredAt: null,
};

describe('transfer API contract', () => {
  it('passes aggregate filters and the opaque cursor unchanged', async () => {
    let received = new URL('https://example.invalid');
    server.use(
      mockHttp.get('*/api/v1/transfers', ({ request }) => {
        received = new URL(request.url);
        return HttpResponse.json({ items: [transfer], nextCursor: 'next-transfer' });
      }),
    );
    await expect(
      listTransfers({ accountId: 1, pageSize: 25 }, 'opaque-transfer-cursor'),
    ).resolves.toEqual({ items: [transfer], nextCursor: 'next-transfer' });
    expect(received.searchParams.get('accountId')).toBe('1');
    expect(received.searchParams.get('pageSize')).toBe('25');
    expect(received.searchParams.get('cursor')).toBe('opaque-transfer-cursor');
  });

  it('preserves caller intent keys for create and aggregate correction', async () => {
    const keys: string[] = [];
    server.use(
      mockHttp.post('*/api/v1/transfers', async ({ request }) => {
        keys.push(request.headers.get('idempotency-key') ?? '');
        expect(await request.json()).toEqual(payload);
        return HttpResponse.json(transfer, { status: 201 });
      }),
      mockHttp.post('*/api/v1/transfers/9/correction', async ({ request }) => {
        keys.push(request.headers.get('idempotency-key') ?? '');
        expect(await request.json()).toEqual(payload);
        return HttpResponse.json(
          { ...transfer, transferGroupId: 10, correctsTransferGroupId: 9 },
          { status: 201 },
        );
      }),
    );
    await createTransfer(payload, 'transfer-create-intent');
    await correctTransfer(9, payload, 'transfer-correction-intent');
    expect(keys).toEqual(['transfer-create-intent', 'transfer-correction-intent']);
  });
});
