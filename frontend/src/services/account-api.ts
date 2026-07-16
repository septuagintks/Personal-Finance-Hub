import type { components } from '../generated/api-types';
import { http } from './http';

export type Account = components['schemas']['Account'];
export type AccountBalance = components['schemas']['Balance'];
export type CreateAccountRequest = components['schemas']['CreateAccountRequest'];
export type UpdateAccountRequest = components['schemas']['UpdateAccountRequest'];
export type AccountListStatus = 'active' | 'archived' | 'all';

export interface AccountResource {
  account: Account;
  etag: string;
}

function requireStrongEtag(value: unknown): string {
  if (typeof value !== 'string' || !/^"[1-9][0-9]*"$/.test(value)) {
    throw new Error('Account response did not include a strong version ETag.');
  }
  return value;
}

export async function listAccounts(
  status: AccountListStatus,
  signal?: AbortSignal,
): Promise<Account[]> {
  const response = await http.get<Account[]>('/api/v1/accounts', {
    params: { status },
    signal,
  });
  return response.data;
}

export async function createAccount(
  payload: CreateAccountRequest,
  signal?: AbortSignal,
): Promise<Account> {
  const response = await http.post<Account>('/api/v1/accounts', payload, {
    signal,
  });
  return response.data;
}

export async function getAccount(
  accountId: number,
  signal?: AbortSignal,
): Promise<AccountResource> {
  const response = await http.get<Account>(`/api/v1/accounts/${accountId}`, { signal });
  return {
    account: response.data,
    etag: requireStrongEtag(response.headers.etag),
  };
}

export async function getAccountBalance(
  accountId: number,
  signal?: AbortSignal,
): Promise<AccountBalance> {
  const response = await http.get<AccountBalance>(`/api/v1/accounts/${accountId}/balance`, {
    signal,
  });
  return response.data;
}

export async function updateAccount(
  accountId: number,
  etag: string,
  payload: UpdateAccountRequest,
  signal?: AbortSignal,
): Promise<AccountResource> {
  const response = await http.put<Account>(`/api/v1/accounts/${accountId}`, payload, {
    headers: { 'If-Match': etag },
    _pfhReplayAfterRefresh: true,
    signal,
  });
  return {
    account: response.data,
    etag: requireStrongEtag(response.headers.etag),
  };
}

export async function archiveAccount(
  accountId: number,
  etag: string,
  signal?: AbortSignal,
): Promise<void> {
  await http.post(`/api/v1/accounts/${accountId}/archive`, undefined, {
    headers: { 'If-Match': etag },
    _pfhReplayAfterRefresh: true,
    signal,
  });
}

export async function restoreAccount(
  accountId: number,
  etag: string,
  signal?: AbortSignal,
): Promise<void> {
  await http.post(`/api/v1/accounts/${accountId}/restore`, undefined, {
    headers: { 'If-Match': etag },
    _pfhReplayAfterRefresh: true,
    signal,
  });
}

export async function deleteAccount(accountId: number, signal?: AbortSignal): Promise<void> {
  await http.delete(`/api/v1/accounts/${accountId}`, {
    params: { confirmations: 3 },
    signal,
  });
}
