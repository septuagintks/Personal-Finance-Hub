import { expect, test } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';
import type { Page } from '@playwright/test';

const refreshEndpoint = '**/api/v1/web/auth/refresh';

const preferencePayload = {
  baseCurrency: 'CNY',
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'yyyy-MM-dd',
  numberFormat: 'standard',
  theme: 'system',
  defaultHomePage: 'dashboard',
  defaultReportPeriod: 'current_month',
};

const currencyPayload = [
  { code: 'CNY', symbol: '\u00a5', precision: 2, displayName: 'Chinese Yuan', isCrypto: false },
  { code: 'USD', symbol: '$', precision: 2, displayName: 'US Dollar', isCrypto: false },
];

const dashboardPayload = {
  baseCurrency: 'CNY',
  netWorth: {
    baseCurrency: 'CNY',
    totalAssets: '100',
    totalLiabilities: '0',
    netWorth: '100',
    generatedAt: '2026-07-16T00:00:00Z',
  },
  monthlyIncome: '20',
  monthlyExpense: '5',
  assetDistribution: [],
  topExpenseCategories: [],
  reportPeriodStart: '2026-07-01T00:00:00Z',
  reportPeriodEnd: '2026-08-01T00:00:00Z',
  generatedAt: '2026-07-16T00:00:00Z',
};

async function routeUserContext(page: Page): Promise<void> {
  await page.route('**/api/v1/users/me/preferences', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(preferencePayload),
    });
  });
  await page.route('**/api/v1/currencies', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(currencyPayload),
    });
  });
}

async function routeDashboard(page: Page): Promise<void> {
  await page.route('**/api/v1/reports/dashboard-summary', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(dashboardPayload),
    });
  });
}

async function expectNoAccessibilityViolations(
  page: import('@playwright/test').Page,
): Promise<void> {
  const result = await new AxeBuilder({ page }).analyze();
  expect(result.violations).toEqual([]);
}

for (const viewport of [
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test.describe(viewport.name, () => {
    test.beforeEach(async ({ page }) => {
      await page.setViewportSize({ width: viewport.width, height: viewport.height });
      await page.route(refreshEndpoint, async (route) => {
        await route.fulfill({
          status: 401,
          contentType: 'application/json',
          body: JSON.stringify({
            error_code: 'UNAUTHORIZED',
            message: 'Invalid or expired access token',
            trace_id: 'e2e-anonymous',
            retryable: false,
            field_errors: [],
          }),
        });
      });
    });

    test('public product entry is usable without horizontal overflow', async ({ page }) => {
      let refreshRequests = 0;
      page.on('request', (request) => {
        if (request.url().includes('/api/v1/web/auth/refresh')) refreshRequests += 1;
      });
      await page.goto('/');
      await expect(page.getByRole('heading', { name: "Candy's Ledger" })).toBeVisible();
      await expect(page.getByRole('link', { name: /Start a private ledger/ })).toBeVisible();
      const overflow = await page.evaluate(
        () => document.documentElement.scrollWidth > document.documentElement.clientWidth,
      );
      expect(overflow).toBe(false);
      expect(refreshRequests).toBe(0);
      await expectNoAccessibilityViolations(page);
    });

    test('guest can navigate between authentication views', async ({ page }) => {
      await page.goto('/login');
      await expect(
        page.getByRole('heading', { name: 'Pick up where you left off.' }),
      ).toBeVisible();
      await page.getByRole('link', { name: 'Create an account' }).click();
      await expect(
        page.getByRole('heading', { name: 'Make room for better decisions.' }),
      ).toBeVisible();
      await expect(page.locator('input[name="new-password"]')).toHaveAttribute(
        'autocomplete',
        'new-password',
      );
      await expectNoAccessibilityViolations(page);
    });

    test('registration preloads user context before opening the dashboard', async ({ page }) => {
      await routeDashboard(page);
      let preferenceAuthorization = '';
      await page.route('**/api/v1/web/auth/register', async (route) => {
        await route.fulfill({
          status: 201,
          contentType: 'application/json',
          body: JSON.stringify({
            userId: 42,
            accessToken: 'registered-access',
            expiresIn: 900,
            tokenType: 'Bearer',
          }),
        });
      });
      await page.route('**/api/v1/users/me/preferences', async (route) => {
        preferenceAuthorization = route.request().headers().authorization ?? '';
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify(preferencePayload),
        });
      });
      await page.route('**/api/v1/currencies', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify(currencyPayload),
        });
      });

      await page.goto('/register');
      await page.locator('input[name="username"]').fill('new-user@example.com');
      await page.locator('input[name="new-password"]').fill('correct horse battery staple');
      await page
        .locator('input[name="new-password-confirmation"]')
        .fill('correct horse battery staple');
      await page.getByRole('button', { name: 'Create private ledger' }).click();

      await expect(page).toHaveURL(/\/dashboard$/);
      await expect(page.getByRole('heading', { name: 'Good morning.' })).toBeVisible();
      expect(preferenceAuthorization).toBe('Bearer registered-access');
    });

    test('login and logout keep credentials out of browser storage', async ({ page }) => {
      await routeUserContext(page);
      await routeDashboard(page);
      await page.route('**/api/v1/web/auth/login', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          headers: {
            'Set-Cookie':
              'pfh_refresh=e2e-cookie; Path=/api/v1/web/auth; HttpOnly; Secure; SameSite=Strict',
          },
          body: JSON.stringify({ accessToken: 'e2e-access', expiresIn: 900, tokenType: 'Bearer' }),
        });
      });
      let logoutAuthorization = '';
      await page.route('**/api/v1/web/auth/logout', async (route) => {
        logoutAuthorization = route.request().headers().authorization ?? '';
        await route.fulfill({ status: 204 });
      });

      await page.goto('/login');
      await page.getByLabel('Username').fill('web-user@example.com');
      await page.locator('input[name="password"]').fill('correct horse battery staple');
      await page.getByRole('button', { name: 'Sign in' }).click();
      await expect(page).toHaveURL(/\/dashboard$/);
      await expect(page.getByRole('heading', { name: 'Good morning.' })).toBeVisible();
      await expectNoAccessibilityViolations(page);

      const stored = await page.evaluate(async () => ({
        local: { ...window.localStorage },
        session: { ...window.sessionStorage },
        databases:
          'databases' in indexedDB
            ? (await indexedDB.databases()).map((database) => database.name)
            : [],
      }));
      expect(JSON.stringify(stored)).not.toContain('e2e-access');
      expect(JSON.stringify(stored)).not.toContain('e2e-cookie');

      if (viewport.name === 'mobile') {
        await page.getByRole('button', { name: 'Open navigation' }).click();
      }
      await page.getByRole('button', { name: 'Sign out' }).click();
      await expect(page).toHaveURL(/\/$/);
      expect(logoutAuthorization).toBe('Bearer e2e-access');
    });

    test('refresh failure clears an established session without replaying the request', async ({
      page,
    }) => {
      await routeUserContext(page);
      await page.route('**/api/v1/web/auth/login', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            accessToken: 'short-lived-access',
            expiresIn: 1,
            tokenType: 'Bearer',
          }),
        });
      });

      let dashboardRequests = 0;
      await page.route('**/api/v1/reports/dashboard-summary', async (route) => {
        dashboardRequests += 1;
        await route.fulfill({
          status: 401,
          contentType: 'application/json',
          body: JSON.stringify({
            error_code: 'UNAUTHORIZED',
            message: 'The access token has expired.',
            trace_id: 'e2e-expired-session',
            retryable: false,
            field_errors: [],
          }),
        });
      });

      await page.goto('/login');
      await page.getByLabel('Username').fill('web-user@example.com');
      await page.locator('input[name="password"]').fill('correct horse battery staple');
      await page.getByRole('button', { name: 'Sign in' }).click();

      await expect(page).toHaveURL(
        (url) => url.pathname === '/login' && url.searchParams.get('redirect') === '/dashboard',
      );
      expect(dashboardRequests).toBe(1);
      const authorization = await page.evaluate(() => ({
        local: { ...window.localStorage },
        session: { ...window.sessionStorage },
      }));
      expect(JSON.stringify(authorization)).not.toContain('short-lived-access');
    });

    test('an expired safe request refreshes once and replays with the new token', async ({
      page,
    }) => {
      await page.unroute(refreshEndpoint);
      await routeUserContext(page);
      const dashboardAuthorizations: string[] = [];
      let refreshRequests = 0;

      await page.route('**/api/v1/web/auth/login', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            accessToken: 'expiring-access',
            expiresIn: 1,
            tokenType: 'Bearer',
          }),
        });
      });
      await page.route(refreshEndpoint, async (route) => {
        refreshRequests += 1;
        if (refreshRequests === 1) {
          await route.fulfill({
            status: 401,
            contentType: 'application/json',
            body: JSON.stringify({
              error_code: 'UNAUTHORIZED',
              message: 'No refresh session is available.',
              trace_id: 'e2e-refresh-anonymous',
              retryable: false,
              field_errors: [],
            }),
          });
          return;
        }
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            accessToken: 'refreshed-access',
            expiresIn: 900,
            tokenType: 'Bearer',
          }),
        });
      });
      await page.route('**/api/v1/reports/dashboard-summary', async (route) => {
        const authorization = route.request().headers().authorization ?? '';
        dashboardAuthorizations.push(authorization);
        if (dashboardAuthorizations.length === 1) {
          await route.fulfill({
            status: 401,
            contentType: 'application/json',
            body: JSON.stringify({
              error_code: 'UNAUTHORIZED',
              message: 'The access token has expired.',
              trace_id: 'e2e-refresh-success',
              retryable: false,
              field_errors: [],
            }),
          });
          return;
        }
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify(dashboardPayload),
        });
      });

      await page.goto('/login');
      await page.getByLabel('Username').fill('web-user@example.com');
      await page.locator('input[name="password"]').fill('correct horse battery staple');
      await page.getByRole('button', { name: 'Sign in' }).click();

      await expect(page).toHaveURL(/\/dashboard$/);
      await expect(page.getByRole('heading', { name: 'Good morning.' })).toBeVisible();
      expect(refreshRequests).toBe(2);
      expect(dashboardAuthorizations).toEqual([
        'Bearer expiring-access',
        'Bearer refreshed-access',
      ]);
    });

    test('direct navigation and reload restore context before the protected page loads', async ({
      page,
    }) => {
      await page.unroute(refreshEndpoint);
      const requestOrder: string[] = [];
      let refreshRequests = 0;
      let preferenceRequests = 0;

      await page.route(refreshEndpoint, async (route) => {
        refreshRequests += 1;
        requestOrder.push(`refresh-${refreshRequests}`);
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            accessToken: `restored-access-${refreshRequests}`,
            expiresIn: 900,
            tokenType: 'Bearer',
          }),
        });
      });
      await page.route('**/api/v1/users/me/preferences', async (route) => {
        preferenceRequests += 1;
        requestOrder.push('preference');
        expect(route.request().headers().authorization).toMatch(/^Bearer restored-access-/);
        if (preferenceRequests === 1) {
          await route.fulfill({
            status: 401,
            contentType: 'application/json',
            body: JSON.stringify({
              error_code: 'UNAUTHORIZED',
              message: 'The access token expired during preload.',
              trace_id: 'e2e-preload-refresh',
              retryable: false,
              field_errors: [],
            }),
          });
          return;
        }
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify(preferencePayload),
        });
      });
      await page.route('**/api/v1/currencies', async (route) => {
        requestOrder.push('currencies');
        expect(route.request().headers().authorization).toMatch(/^Bearer restored-access-/);
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify(currencyPayload),
        });
      });
      await page.route('**/api/v1/reports/dashboard-summary', async (route) => {
        requestOrder.push('dashboard');
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify(dashboardPayload),
        });
      });

      await page.goto('/dashboard');
      await expect(page.getByRole('heading', { name: 'Good morning.' })).toBeVisible();
      const firstDashboardIndex = requestOrder.indexOf('dashboard');
      const firstPreferenceIndexes = requestOrder
        .map((entry, index) => (entry === 'preference' ? index : -1))
        .filter((index) => index >= 0);
      expect(firstDashboardIndex).toBeGreaterThan(firstPreferenceIndexes[1]);
      expect(firstDashboardIndex).toBeGreaterThan(requestOrder.indexOf('currencies'));

      await page.reload();
      await expect(page.getByRole('heading', { name: 'Good morning.' })).toBeVisible();
      expect(refreshRequests).toBe(3);
      expect(requestOrder.filter((entry) => entry === 'preference')).toHaveLength(3);
      expect(requestOrder.filter((entry) => entry === 'currencies')).toHaveLength(2);
      expect(requestOrder.filter((entry) => entry === 'dashboard')).toHaveLength(2);
    });

    test('context preload failure never exposes a protected authenticated state', async ({
      page,
    }) => {
      await page.route('**/api/v1/web/auth/login', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            accessToken: 'orphaned-access',
            expiresIn: 900,
            tokenType: 'Bearer',
          }),
        });
      });
      await page.route('**/api/v1/users/me/preferences', async (route) => {
        await route.abort('connectionfailed');
      });
      await page.route('**/api/v1/currencies', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify(currencyPayload),
        });
      });

      await page.goto('/login');
      await page.getByLabel('Username').fill('web-user@example.com');
      await page.locator('input[name="password"]').fill('correct horse battery staple');
      await page.getByRole('button', { name: 'Sign in' }).click();

      await expect(page).toHaveURL(/\/login$/);
      await expect(page.getByRole('alert')).toContainText('The service is unavailable right now.');
      const stored = await page.evaluate(() => ({
        local: { ...window.localStorage },
        session: { ...window.sessionStorage },
      }));
      expect(JSON.stringify(stored)).not.toContain('orphaned-access');
    });
  });
}

test('logout broadcasts state only and removes both tabs from protected routes', async ({
  context,
}) => {
  let refreshRequests = 0;
  await context.route('**/api/v1/web/auth/login', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ accessToken: 'tab-one-access', expiresIn: 900, tokenType: 'Bearer' }),
    });
  });
  await context.route(refreshEndpoint, async (route) => {
    refreshRequests += 1;
    if (refreshRequests === 1) {
      await route.fulfill({
        status: 401,
        contentType: 'application/json',
        body: JSON.stringify({
          error_code: 'UNAUTHORIZED',
          message: 'No refresh session is available.',
          trace_id: 'e2e-first-tab-anonymous',
          retryable: false,
          field_errors: [],
        }),
      });
      return;
    }
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ accessToken: 'tab-two-access', expiresIn: 900, tokenType: 'Bearer' }),
    });
  });
  await context.route('**/api/v1/users/me/preferences', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(preferencePayload),
    });
  });
  await context.route('**/api/v1/currencies', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(currencyPayload),
    });
  });
  await context.route('**/api/v1/reports/dashboard-summary', async (route) => {
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify(dashboardPayload),
    });
  });
  await context.route('**/api/v1/web/auth/logout', async (route) => {
    await route.fulfill({ status: 204 });
  });

  const firstPage = await context.newPage();
  await firstPage.goto('/login');
  await firstPage.evaluate(() => {
    const messages: unknown[] = [];
    const observer = new BroadcastChannel('pfh-web-session');
    observer.addEventListener('message', (event) => messages.push(event.data));
    Object.assign(window, { __pfhSessionMessages: messages, __pfhSessionObserver: observer });
  });
  await firstPage.getByLabel('Username').fill('web-user@example.com');
  await firstPage.locator('input[name="password"]').fill('correct horse battery staple');
  await firstPage.getByRole('button', { name: 'Sign in' }).click();
  await expect(firstPage).toHaveURL(/\/dashboard$/);

  const secondPage = await context.newPage();
  await secondPage.goto('/dashboard');
  await expect(secondPage.getByRole('heading', { name: 'Good morning.' })).toBeVisible();

  await firstPage.getByRole('button', { name: 'Sign out' }).click();
  await expect(firstPage).toHaveURL(/\/$/);
  await expect(secondPage).toHaveURL(
    (url) => url.pathname === '/login' && url.searchParams.get('redirect') === '/dashboard',
  );

  const projection = await firstPage.evaluate(() => ({
    messages: (window as typeof window & { __pfhSessionMessages?: unknown[] }).__pfhSessionMessages,
    local: { ...window.localStorage },
    session: { ...window.sessionStorage },
  }));
  expect(projection.messages).toContainEqual({ type: 'session-state', state: 'anonymous' });
  expect(JSON.stringify(projection)).not.toContain('tab-one-access');
  expect(JSON.stringify(projection)).not.toContain('tab-two-access');
});
