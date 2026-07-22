import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page, type Route } from '@playwright/test';

const preference = {
  baseCurrency: 'CNY',
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'YYYY-MM-DD',
  numberFormat: '1,234.56',
  theme: 'system',
  defaultHomePage: 'dashboard',
  defaultReportPeriod: 'current_month',
  customReportStartMonth: null,
  customReportEndMonth: null,
};

const account = {
  id: 7,
  name: 'Daily wallet',
  type: 'cash',
  subtype: 'wallet',
  category: 'asset',
  currencyCode: 'CNY',
  description: '',
  isArchived: false,
  archivedAt: null,
  createdAt: '2026-07-18T00:00:00Z',
  updatedAt: '2026-07-18T00:00:00Z',
  version: 3,
};

async function json(route: Route, body: unknown, status = 200): Promise<void> {
  await route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
}

async function installSession(page: Page, role: 'USER' | 'OPERATOR'): Promise<void> {
  await page.route('**/api/v1/web/auth/refresh', (route) =>
    json(route, {
      accessToken: `${role.toLowerCase()}-access`,
      expiresIn: 900,
      tokenType: 'Bearer',
      roles: [role],
    }),
  );
  await page.route('**/api/v1/users/me/preferences', (route) => json(route, preference));
  await page.route('**/api/v1/currencies', (route) =>
    json(route, [
      { code: 'CNY', symbol: 'CNY', precision: 2, displayName: 'Chinese Yuan', isCrypto: false },
    ]),
  );
}

async function expectAccessibleAndContained(page: Page): Promise<void> {
  const result = await new AxeBuilder({ page }).analyze();
  expect(result.violations).toEqual([]);
  const overflow = await page.evaluate(
    () => document.documentElement.scrollWidth > document.documentElement.clientWidth,
  );
  expect(overflow).toBe(false);
}

for (const viewport of [
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test.describe(`S10 ${viewport.name}`, () => {
    test.beforeEach(async ({ page }) => {
      await page.setViewportSize({ width: viewport.width, height: viewport.height });
    });

    test('user filters audit facts and rebuilds owned balance caches', async ({ page }) => {
      await installSession(page, 'USER');
      let auditResource = '';
      let rebuildCalls = 0;
      await page.route('**/api/v1/accounts?**', (route) => json(route, [account]));
      await page.route('**/api/v1/maintenance/audit-logs?**', (route) => {
        auditResource = new URL(route.request().url()).searchParams.get('resourceType') ?? '';
        return json(route, {
          items: [
            {
              id: 51,
              action: 'update',
              resourceType: 'account',
              resourceId: '7',
              result: 'success',
              traceId: 'trace-maintenance-1',
              occurredAt: '2026-07-18T01:30:00Z',
            },
          ],
          nextCursor: null,
        });
      });
      await page.route('**/api/v1/maintenance/accounts/balance-cache/rebuild', (route) => {
        rebuildCalls += 1;
        return json(route, {
          accounts: [
            {
              accountId: 7,
              currencyCode: 'CNY',
              balance: '125.50000000',
              lastTransactionId: 19,
              sourceVersion: 6,
              cacheVersion: 4,
              rebuiltAt: '2026-07-18T02:00:00Z',
            },
          ],
        });
      });

      await page.goto('/maintenance');
      await expect(page.getByRole('heading', { name: 'Maintenance' })).toBeVisible();
      await expect(page.getByText('trace-maintenance-1')).toBeVisible();
      await expect(page.getByRole('link', { name: 'Operations' })).toHaveCount(0);

      await page.getByPlaceholder('account').fill('account');
      await page.getByRole('button', { name: 'Apply' }).click();
      await expect.poll(() => auditResource).toBe('account');
      await page.getByRole('button', { name: 'Rebuild all' }).click();
      await expect(page.getByText('125.5 CNY')).toBeVisible();
      expect(rebuildCalls).toBe(1);
      await expectAccessibleAndContained(page);
    });

    test('operator sees bounded runtime facts and retries a sanitized dead letter', async ({
      page,
    }) => {
      await installSession(page, 'OPERATOR');
      let retryKey = '';
      await page.route('**/api/v1/operations/summary', (route) =>
        json(route, {
          generatedAt: '2026-07-18T02:00:00Z',
          outbox: { pending: 2, processing: 1, published: 20, failed: 1, deadLetter: 1 },
          handlerReceipts: { count: 20, latestAt: '2026-07-18T01:59:00Z' },
          expiredIdempotency: 4,
          leases: [
            { jobName: 'outbox-publisher', active: true, leaseUntil: '2026-07-18T02:01:00Z' },
          ],
          jobs: [
            {
              name: 'outbox-publisher',
              schedulerStarted: true,
              running: false,
              executionSequence: 12,
              lastResult: 'succeeded',
              lastStartedAt: '2026-07-18T01:59:00Z',
              lastFinishedAt: '2026-07-18T01:59:01Z',
              lastDurationMs: 18,
            },
          ],
        }),
      );
      await page.route('**/api/v1/operations/dead-letters?**', (route) =>
        json(route, {
          items: [
            {
              id: 'dead-letter-1',
              eventName: 'TransactionCreated',
              aggregateType: 'transaction',
              aggregateId: '19',
              retryCount: 5,
              maxRetryCount: 5,
              lastFailedHandler: 'audit-projector',
              lastFailedAt: '2026-07-18T01:50:00Z',
              createdAt: '2026-07-18T01:40:00Z',
            },
          ],
          nextCursor: null,
        }),
      );
      await page.route('**/api/v1/operations/dead-letters/dead-letter-1/retry', (route) => {
        retryKey = route.request().headers()['idempotency-key'] ?? '';
        return json(
          route,
          { outboxId: 'dead-letter-1', replayed: false, status: 'retry_scheduled' },
          202,
        );
      });

      await page.goto('/operations');
      await expect(page.getByRole('heading', { name: 'Operations' })).toBeVisible();
      await expect(page.getByText('TransactionCreated')).toBeVisible();
      await expect(page.getByText('outbox-publisher', { exact: true }).first()).toBeVisible();
      await page.getByRole('button', { name: 'Retry', exact: true }).click();
      await expect.poll(() => retryKey).toMatch(/^dead-letter-\S+$/);
      await expectAccessibleAndContained(page);
    });
  });
}

test('ordinary user direct operations navigation resolves to a guarded 403 view', async ({
  page,
}) => {
  await installSession(page, 'USER');
  let operationsRequests = 0;
  page.on('request', (request) => {
    if (request.url().includes('/api/v1/operations/')) operationsRequests += 1;
  });

  await page.goto('/operations');
  await expect(page).toHaveURL(/\/forbidden$/);
  await expect(page.getByRole('heading', { name: 'Operator access required' })).toBeVisible();
  expect(operationsRequests).toBe(0);
});
