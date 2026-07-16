import { HttpResponse, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import { createPinia, setActivePinia } from 'pinia';
import { getAccessToken } from '../services/http';
import { server } from '../test/server';
import { useSessionStore } from './session';
import { useAccountStore } from './accounts';
import { useUserContextStore } from './user-context';

const preference = {
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'yyyy-MM-dd',
  numberFormat: 'standard',
  theme: 'system' as const,
  defaultHomePage: 'dashboard' as const,
  defaultReportPeriod: 'current_month' as const,
};

function installSessionHandlers(): void {
  server.use(
    mockHttp.post('*/api/v1/web/auth/login', async ({ request }) => {
      const body = (await request.json()) as { username: string };
      const user = body.username.startsWith('user-a') ? 'a' : 'b';
      return HttpResponse.json({
        accessToken: `access-${user}`,
        expiresIn: 900,
        tokenType: 'Bearer',
      });
    }),
    mockHttp.post('*/api/v1/web/auth/logout', () => new HttpResponse(null, { status: 204 })),
    mockHttp.get('*/api/v1/users/me/preferences', ({ request }) => {
      const isUserA = request.headers.get('authorization') === 'Bearer access-a';
      return HttpResponse.json({ ...preference, baseCurrency: isUserA ? 'USD' : 'CNY' });
    }),
    mockHttp.get('*/api/v1/currencies', () =>
      HttpResponse.json([
        { code: 'CNY', symbol: 'CNY', precision: 2, displayName: 'CNY', isCrypto: false },
      ]),
    ),
    mockHttp.get('*/api/v1/accounts', () =>
      HttpResponse.json([
        {
          id: 11,
          name: 'Session account',
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
        },
      ]),
    ),
  );
}

describe('session context lifecycle', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('clears user A context before user B becomes authenticated', async () => {
    installSessionHandlers();
    const session = useSessionStore();
    const context = useUserContextStore();
    const accounts = useAccountStore();

    await session.login({ username: 'user-a@example.com', password: 'correct password' });
    await accounts.loadList('active');
    expect(session.isAuthenticated).toBe(true);
    expect(context.preference?.baseCurrency).toBe('USD');
    expect(accounts.items).toHaveLength(1);

    await session.logout();
    expect(session.isAuthenticated).toBe(false);
    expect(context.preference).toBeNull();
    expect(context.currencies).toEqual([]);
    expect(accounts.items).toEqual([]);

    await session.login({ username: 'user-b@example.com', password: 'correct password' });
    expect(session.isAuthenticated).toBe(true);
    expect(context.preference?.baseCurrency).toBe('CNY');
    expect(getAccessToken()).toBe('access-b');
  });

  it('clears the token when required context cannot be loaded', async () => {
    installSessionHandlers();
    server.use(mockHttp.get('*/api/v1/users/me/preferences', () => HttpResponse.error()));
    const session = useSessionStore();
    const context = useUserContextStore();

    await expect(
      session.login({ username: 'user-a@example.com', password: 'correct password' }),
    ).rejects.toBeDefined();

    expect(session.status).toBe('anonymous');
    expect(session.isAuthenticated).toBe(false);
    expect(context.preference).toBeNull();
    expect(getAccessToken()).toBeNull();
  });
});
