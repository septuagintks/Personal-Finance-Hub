import { HttpResponse, http as mockHttp } from 'msw';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { server } from '../test/server';
import {
  clearRefreshState,
  getAccessToken,
  http,
  registerRefreshHandler,
  registerSessionExpiredHandler,
  setAccessToken,
} from './http';

describe('HTTP refresh lifecycle', () => {
  afterEach(() => {
    clearRefreshState();
    setAccessToken(null);
    registerRefreshHandler(async () => {
      throw new Error('No refresh handler installed for this test.');
    });
    registerSessionExpiredHandler(() => undefined);
  });

  it('aborts a stale refresh without clearing a newer session', async () => {
    server.use(
      mockHttp.get('*/api/v1/test-refresh-race', () =>
        HttpResponse.json({ message: 'expired' }, { status: 401 }),
      ),
    );

    let refreshSignal: AbortSignal | undefined;
    const accepted = vi.fn();
    let resolveRefresh:
      ((result: { accessToken: string; onAccepted: () => void }) => void) | undefined;
    const refreshStarted = new Promise<void>((resolve) => {
      registerRefreshHandler(
        (signal) =>
          new Promise<{ accessToken: string; onAccepted: () => void }>((resolveToken) => {
            refreshSignal = signal;
            resolveRefresh = resolveToken;
            resolve();
          }),
      );
    });
    const expired = vi.fn();
    registerSessionExpiredHandler(expired);
    setAccessToken('old-access');

    const request = http.get('/api/v1/test-refresh-race');
    await refreshStarted;
    clearRefreshState();
    setAccessToken('new-access');
    expect(refreshSignal?.aborted).toBe(true);
    resolveRefresh?.({ accessToken: 'stale-access', onAccepted: accepted });

    await expect(request).rejects.toBeDefined();
    expect(getAccessToken()).toBe('new-access');
    expect(accepted).not.toHaveBeenCalled();
    expect(expired).not.toHaveBeenCalled();
  });
});
