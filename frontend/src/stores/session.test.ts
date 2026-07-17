import { HttpResponse, delay, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import { createPinia, setActivePinia } from 'pinia';
import { getAccessToken, http } from '../services/http';
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
        roles: ['USER'],
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
    expect(session.roles).toEqual(['USER']);
    expect(session.isOperator).toBe(false);
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

  it('waits for an in-flight refresh before revoking the rotated session', async () => {
    installSessionHandlers();
    const session = useSessionStore();
    await session.login({ username: 'user-a@example.com', password: 'correct password' });

    let releaseRefresh: () => void = () => {};
    const refreshGate = new Promise<void>((resolve) => {
      releaseRefresh = resolve;
    });
    let markRefreshStarted: () => void = () => {};
    const refreshStarted = new Promise<void>((resolve) => {
      markRefreshStarted = resolve;
    });
    let probeCalls = 0;
    let logoutAuthorization = '';
    server.use(
      mockHttp.get('*/api/v1/session-race-probe', () => {
        probeCalls += 1;
        return probeCalls === 1
          ? HttpResponse.json({}, { status: 401 })
          : HttpResponse.json({ ok: true });
      }),
      mockHttp.post('*/api/v1/web/auth/refresh', async () => {
        markRefreshStarted();
        await refreshGate;
        return HttpResponse.json({
          accessToken: 'access-rotated',
          expiresIn: 900,
          tokenType: 'Bearer',
          roles: ['USER'],
        });
      }),
      mockHttp.post('*/api/v1/web/auth/logout', ({ request }) => {
        logoutAuthorization = request.headers.get('authorization') ?? '';
        return new HttpResponse(null, { status: 204 });
      }),
    );

    const probe = http.get('/api/v1/session-race-probe');
    await refreshStarted;
    const logout = session.logout();
    await delay(20);
    expect(logoutAuthorization).toBe('');

    releaseRefresh();
    await expect(probe).resolves.toBeDefined();
    await expect(logout).resolves.toBeUndefined();
    expect(logoutAuthorization).toBe('Bearer access-rotated');
    expect(session.status).toBe('anonymous');
    expect(getAccessToken()).toBeNull();
  });

  it('updates the current role from a rotated server session', async () => {
    installSessionHandlers();
    const session = useSessionStore();
    await session.login({ username: 'user-a@example.com', password: 'correct password' });

    let probeCalls = 0;
    server.use(
      mockHttp.get('*/api/v1/role-probe', () => {
        probeCalls += 1;
        return probeCalls === 1
          ? HttpResponse.json({}, { status: 401 })
          : HttpResponse.json({ ok: true });
      }),
      mockHttp.post('*/api/v1/web/auth/refresh', () =>
        HttpResponse.json({
          accessToken: 'operator-access',
          expiresIn: 900,
          tokenType: 'Bearer',
          roles: ['OPERATOR'],
        }),
      ),
    );

    await expect(http.get('/api/v1/role-probe')).resolves.toBeDefined();
    expect(session.roles).toEqual(['OPERATOR']);
    expect(session.isOperator).toBe(true);
    expect(getAccessToken()).toBe('operator-access');
  });
});
