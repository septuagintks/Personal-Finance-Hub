import { expect, test, type CDPSession, type Page, type Route } from '@playwright/test';

const preference = {
  baseCurrency: 'CNY',
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'YYYY-MM-DD',
  numberFormat: '1,234.56',
  theme: 'system',
  defaultHomePage: 'transactions',
  defaultReportPeriod: 'last_12_months',
  customReportStartMonth: null,
  customReportEndMonth: null,
};

const account = {
  id: 1,
  name: 'Daily Wallet',
  type: 'digital_wallet',
  subtype: 'wallet',
  category: 'asset',
  currencyCode: 'CNY',
  description: '',
  isArchived: false,
  archivedAt: null,
  createdAt: '2026-01-01T00:00:00Z',
  updatedAt: '2026-01-01T00:00:00Z',
  version: 1,
};

async function json(route: Route, body: unknown): Promise<void> {
  await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
}

function transaction(id: number) {
  return {
    id,
    accountId: 1,
    type: 'expense',
    amount: String((id % 500) + 1),
    currencyCode: 'CNY',
    categoryId: 10,
    categoryName: 'Everyday',
    categoryDeleted: false,
    tags: Array.from({ length: 8 }, (_, index) => ({
      id: index + 1,
      name: `tag-${index + 1}`,
      isDeleted: false,
    })),
    transferGroupId: null,
    correctsTransactionId: null,
    correctedByTransactionId: null,
    description: `Bounded ledger entry ${id}`,
    occurredAt: '2026-07-18T04:00:00Z',
    createdAt: '2026-07-18T04:00:00Z',
    deletedAt: null,
  };
}

function reportAnalysis(dimension: 'root_category' | 'account' | 'tag') {
  return {
    baseCurrency: 'CNY',
    valuationAt: '2026-07-18T04:00:00Z',
    rateStatus: 'historical',
    reportPeriodStart: '2016-08-01T00:00:00Z',
    reportPeriodEnd: '2026-08-01T00:00:00Z',
    dimension,
    dimensionOverlaps: dimension === 'tag',
    netWorthTrend: Array.from({ length: 120 }, (_, index) => ({
      period: `${2016 + Math.floor((index + 7) / 12)}-${String(((index + 7) % 12) + 1).padStart(2, '0')}`,
      totalAssets: String(10_000 + index * 20),
      totalLiabilities: String(-2_000 + index * 5),
      netWorth: String(8_000 + index * 25),
      valuedAt: '2026-07-18T04:00:00Z',
      rateStatus: 'historical',
    })),
    breakdown: Array.from({ length: 200 }, (_, index) => ({
      key: `tag:${index + 1}`,
      label: `Dimension ${index + 1}`,
      income: String(500 + index),
      expense: String(200 + index),
      net: '300',
    })),
  };
}

async function installApi(page: Page): Promise<void> {
  await page.route('**/api/v1/web/auth/refresh', (route) =>
    json(route, {
      accessToken: 'long-session-access',
      expiresIn: 900,
      tokenType: 'Bearer',
      roles: ['USER'],
    }),
  );
  await page.route('**/api/v1/users/me/preferences', (route) => json(route, preference));
  await page.route('**/api/v1/currencies', (route) =>
    json(route, [
      { code: 'CNY', symbol: '¥', precision: 2, displayName: 'Chinese Yuan', isCrypto: false },
    ]),
  );
  await page.route('**/api/v1/accounts**', (route) => json(route, [account]));
  await page.route('**/api/v1/categories**', (route) =>
    json(route, [
      {
        id: 10,
        name: 'Everyday',
        board: 'expense',
        source: 'user',
        parentId: null,
        templateId: null,
        sortOrder: 10,
        isDeleted: false,
        deletedAt: null,
        createdAt: '2026-01-01T00:00:00Z',
        updatedAt: '2026-01-01T00:00:00Z',
        children: [],
      },
    ]),
  );
  await page.route('**/api/v1/tags**', (route) => json(route, []));
  await page.route('**/api/v1/transactions**', (route) => {
    const url = new URL(route.request().url());
    const pageIndex = Number(url.searchParams.get('cursor')?.replace('page-', '') ?? '0');
    const items = Array.from({ length: 50 }, (_, index) => transaction(pageIndex * 50 + index + 1));
    return json(route, {
      items,
      nextCursor: pageIndex < 8 ? `page-${pageIndex + 1}` : null,
    });
  });
  await page.route('**/api/v1/reports/analysis**', (route) => {
    const rawDimension = new URL(route.request().url()).searchParams.get('dimension');
    const dimension =
      rawDimension === 'account' || rawDimension === 'tag' ? rawDimension : 'root_category';
    return json(route, reportAnalysis(dimension));
  });
}

async function collectHeap(session: CDPSession): Promise<number> {
  await session.send('HeapProfiler.collectGarbage');
  const usage = (await session.send('Runtime.getHeapUsage')) as { usedSize: number };
  return usage.usedSize;
}

async function navigate(page: Page, label: 'Reports' | 'Transactions'): Promise<void> {
  await page.getByRole('link', { name: label, exact: true }).click();
  await expect(page.getByRole('heading', { name: label, exact: true })).toBeVisible();
  if (label === 'Reports') {
    await expect(page.locator('.report-chart canvas')).toHaveCount(2);
  } else {
    await expect(page.locator('.ledger-list__row').first()).toBeVisible();
  }
}

test('long SPA sessions retain bounded heap, charts and ledger DOM', async ({
  page,
  browserName,
}) => {
  test.skip(browserName !== 'chromium', 'CDP heap collection is Chromium-specific.');
  test.setTimeout(120_000);
  await installApi(page);
  await page.goto('/transactions');
  await expect(page.getByRole('heading', { name: 'Transactions', exact: true })).toBeVisible();

  for (let pageIndex = 0; pageIndex < 5; pageIndex += 1) {
    await page.getByRole('button', { name: 'Load more' }).click();
    await expect(page.locator('.ledger-list__row')).toHaveCount(
      Math.min((pageIndex + 2) * 50, 200),
    );
  }
  await expect(page.getByRole('button', { name: 'Return to latest' })).toBeVisible();
  expect(await page.locator('.ledger-tags small').count()).toBeLessThanOrEqual(1_000);

  const session = await page.context().newCDPSession(page);
  try {
    for (let cycle = 0; cycle < 5; cycle += 1) {
      await navigate(page, 'Reports');
      await navigate(page, 'Transactions');
    }
    await navigate(page, 'Reports');
    const baselineHeap = await collectHeap(session);

    for (let cycle = 0; cycle < 20; cycle += 1) {
      await navigate(page, 'Transactions');
      await navigate(page, 'Reports');
    }
    const finalHeap = await collectHeap(session);
    const projection = await page.evaluate(() => ({
      canvases: document.querySelectorAll('canvas').length,
      ledgerRows: document.querySelectorAll('.ledger-list__row').length,
      elements: document.querySelectorAll('*').length,
    }));

    expect(finalHeap - baselineHeap).toBeLessThanOrEqual(16 * 1024 * 1024);
    expect(projection.canvases).toBe(2);
    expect(projection.ledgerRows).toBe(0);
    expect(projection.elements).toBeLessThanOrEqual(5_000);
  } finally {
    await session.detach();
  }
});
