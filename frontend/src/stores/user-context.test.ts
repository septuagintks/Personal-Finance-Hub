import { HttpResponse, delay, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import { createPinia, setActivePinia } from 'pinia';
import { server } from '../test/server';
import { useUserContextStore } from './user-context';

const preferenceEndpoint = '*/api/v1/users/me/preferences';
const currenciesEndpoint = '*/api/v1/currencies';

function preference(baseCurrency: string) {
  return {
    baseCurrency,
    locale: 'en-US',
    timezone: 'Asia/Shanghai',
    dateFormat: 'yyyy-MM-dd',
    numberFormat: 'standard',
    theme: 'system' as const,
    defaultHomePage: 'dashboard' as const,
    defaultReportPeriod: 'current_month' as const,
  };
}

function currency(code: string) {
  return { code, symbol: code, precision: 2, displayName: code, isCrypto: false };
}

describe('user context store', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('publishes preferences and currencies together', async () => {
    server.use(
      mockHttp.get(preferenceEndpoint, () => HttpResponse.json(preference('CNY'))),
      mockHttp.get(currenciesEndpoint, () => HttpResponse.json([currency('CNY')])),
    );

    const store = useUserContextStore();
    await expect(store.load()).resolves.toBe(true);

    expect(store.status).toBe('ready');
    expect(store.preference?.baseCurrency).toBe('CNY');
    expect(store.currencies.map(({ code }) => code)).toEqual(['CNY']);
  });

  it('clears data and rejects late responses after logout', async () => {
    server.use(
      mockHttp.get(preferenceEndpoint, async () => {
        await delay(80);
        return HttpResponse.json(preference('USD'));
      }),
      mockHttp.get(currenciesEndpoint, async () => {
        await delay(80);
        return HttpResponse.json([currency('USD')]);
      }),
    );

    const store = useUserContextStore();
    const pending = store.load();
    store.clear();

    await expect(pending).resolves.toBe(false);
    expect(store.status).toBe('idle');
    expect(store.preference).toBeNull();
    expect(store.currencies).toEqual([]);
  });

  it('keeps only the newest user context during a user switch', async () => {
    let preferenceRequest = 0;
    let currencyRequest = 0;
    server.use(
      mockHttp.get(preferenceEndpoint, async () => {
        preferenceRequest += 1;
        const requestNumber = preferenceRequest;
        if (requestNumber === 1) await delay(80);
        return HttpResponse.json(preference(requestNumber === 1 ? 'USD' : 'CNY'));
      }),
      mockHttp.get(currenciesEndpoint, async () => {
        currencyRequest += 1;
        const requestNumber = currencyRequest;
        if (requestNumber === 1) await delay(80);
        return HttpResponse.json([currency(requestNumber === 1 ? 'USD' : 'CNY')]);
      }),
    );

    const store = useUserContextStore();
    const first = store.load();
    await delay(10);
    const second = store.load();

    await expect(second).resolves.toBe(true);
    await expect(first).resolves.toBe(false);
    expect(store.preference?.baseCurrency).toBe('CNY');
    expect(store.currencies.map(({ code }) => code)).toEqual(['CNY']);
  });

  it('does not retain partial data when either request fails', async () => {
    server.use(
      mockHttp.get(preferenceEndpoint, () => HttpResponse.json(preference('CNY'))),
      mockHttp.get(currenciesEndpoint, () => HttpResponse.error()),
    );

    const store = useUserContextStore();
    await expect(store.load()).rejects.toBeDefined();

    expect(store.status).toBe('error');
    expect(store.preference).toBeNull();
    expect(store.currencies).toEqual([]);
  });
});
