import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import {
  archiveAccount,
  createAccount,
  deleteAccount,
  getAccount,
  listAccounts,
  restoreAccount,
  updateAccount,
  type Account,
  type UpdateAccountRequest,
} from './account-api';

const account: Account = {
  id: 7,
  name: 'Daily Wallet',
  type: 'digital_wallet',
  subtype: 'wallet',
  category: 'asset',
  currencyCode: 'CNY',
  description: '',
  isArchived: false,
  archivedAt: null,
  createdAt: '2026-07-16T00:00:00Z',
  updatedAt: '2026-07-16T00:00:00Z',
  version: 3,
};

const update: UpdateAccountRequest = {
  name: 'Primary Wallet',
  type: 'digital_wallet',
  subtype: 'mobile wallet',
  category: 'asset',
  currencyCode: 'CNY',
  description: 'Daily spending',
};

describe('account API contract', () => {
  it('forwards the caller-owned intent key on account creation', async () => {
    let receivedKey = '';
    server.use(
      mockHttp.post('*/api/v1/accounts', ({ request }) => {
        receivedKey = request.headers.get('idempotency-key') ?? '';
        return HttpResponse.json(account, { status: 201 });
      }),
    );

    const payload = {
      name: account.name,
      type: account.type,
      subtype: account.subtype,
      category: account.category,
      currencyCode: account.currencyCode,
      description: account.description,
    };
    await createAccount(payload, 'account-stable-intent');

    expect(receivedKey).toBe('account-stable-intent');
  });

  it('passes the requested archive status to the list endpoint', async () => {
    let status = '';
    server.use(
      mockHttp.get('*/api/v1/accounts', ({ request }) => {
        status = new URL(request.url).searchParams.get('status') ?? '';
        return HttpResponse.json([account]);
      }),
    );

    await expect(listAccounts('archived')).resolves.toEqual([account]);
    expect(status).toBe('archived');
  });

  it('requires and projects the strong ETag from account detail', async () => {
    server.use(
      mockHttp.get('*/api/v1/accounts/7', () =>
        HttpResponse.json(account, { headers: { ETag: '"3"' } }),
      ),
    );

    await expect(getAccount(7)).resolves.toEqual({ account, etag: '"3"' });

    server.use(mockHttp.get('*/api/v1/accounts/7', () => HttpResponse.json(account)));
    await expect(getAccount(7)).rejects.toThrow('strong version ETag');

    server.use(
      mockHttp.get('*/api/v1/accounts/7', () =>
        HttpResponse.json(account, { headers: { ETag: '"4"' } }),
      ),
    );
    await expect(getAccount(7)).rejects.toThrow('strong version ETag');
  });

  it('sends the exact validator on update, archive, and restore', async () => {
    const validators: string[] = [];
    server.use(
      mockHttp.put('*/api/v1/accounts/7', async ({ request }) => {
        validators.push(request.headers.get('if-match') ?? '');
        expect(await request.json()).toEqual(update);
        return HttpResponse.json(
          { ...account, ...update, version: 4 },
          { headers: { ETag: '"4"' } },
        );
      }),
      mockHttp.post('*/api/v1/accounts/7/archive', ({ request }) => {
        validators.push(request.headers.get('if-match') ?? '');
        return new HttpResponse(null, { status: 204 });
      }),
      mockHttp.post('*/api/v1/accounts/7/restore', ({ request }) => {
        validators.push(request.headers.get('if-match') ?? '');
        return new HttpResponse(null, { status: 204 });
      }),
    );

    await updateAccount(7, '"3"', update);
    await archiveAccount(7, '"4"');
    await restoreAccount(7, '"5"');

    expect(validators).toEqual(['"3"', '"4"', '"5"']);
  });

  it('keeps the dangerous delete confirmation count fixed at three', async () => {
    let confirmations = '';
    server.use(
      mockHttp.delete('*/api/v1/accounts/7', ({ request }) => {
        confirmations = new URL(request.url).searchParams.get('confirmations') ?? '';
        return new HttpResponse(null, { status: 204 });
      }),
    );

    await deleteAccount(7);
    expect(confirmations).toBe('3');
  });
});
