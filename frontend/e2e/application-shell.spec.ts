import { expect, test } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';

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
      await page.route('**/api/v1/web/auth/refresh', async (route) => {
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

    test('login and logout keep credentials out of browser storage', async ({ page }) => {
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
      await page.route('**/api/v1/reports/dashboard-summary', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
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
          }),
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
  });
}
