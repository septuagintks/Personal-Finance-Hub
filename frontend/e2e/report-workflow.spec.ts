import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page, type Route } from '@playwright/test';

const preference = {
  baseCurrency: 'CNY',
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'YYYY-MM-DD',
  numberFormat: '1,234.56',
  theme: 'system',
  defaultHomePage: 'reports',
  defaultReportPeriod: 'last_3_months',
};

async function json(route: Route, body: unknown, status = 200): Promise<void> {
  await route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
}

function installApi(page: Page): { dimensions: string[] } {
  const state = { dimensions: [] as string[] };
  void page.route('**/api/v1/web/auth/refresh', (route) =>
    json(route, { accessToken: 'report-access', expiresIn: 900, tokenType: 'Bearer' }),
  );
  void page.route('**/api/v1/users/me/preferences', (route) => json(route, preference));
  void page.route('**/api/v1/currencies', (route) =>
    json(route, [
      { code: 'CNY', symbol: '¥', precision: 2, displayName: 'Chinese Yuan', isCrypto: false },
    ]),
  );
  void page.route('**/api/v1/reports/analysis**', (route) => {
    const url = new URL(route.request().url());
    const dimension = url.searchParams.get('dimension') ?? 'root_category';
    state.dimensions.push(dimension);
    const labels: Record<string, Array<{ key: string; label: string }>> = {
      root_category: [
        { key: 'category:1', label: 'Food' },
        { key: 'category:2', label: 'Transport' },
      ],
      account: [
        { key: 'account:1', label: 'Daily Wallet' },
        { key: 'account:2', label: 'Savings' },
      ],
      tag: [
        { key: 'tag:1', label: 'Recurring' },
        { key: 'tag:2', label: 'Travel' },
      ],
    };
    return json(route, {
      baseCurrency: 'CNY',
      valuationAt: '2026-07-18T02:00:00Z',
      rateStatus: 'historical',
      reportPeriodStart: '2026-01-01T00:00:00Z',
      reportPeriodEnd: '2026-07-18T02:00:00Z',
      dimension,
      dimensionOverlaps: dimension === 'tag',
      netWorthTrend: [
        {
          period: '2026-05',
          totalAssets: '10000',
          totalLiabilities: '-2000',
          netWorth: '8000',
          valuedAt: '2026-05-31T15:59:59Z',
          rateStatus: 'historical',
        },
        {
          period: '2026-06',
          totalAssets: '10800',
          totalLiabilities: '-1800',
          netWorth: '9000',
          valuedAt: '2026-06-30T15:59:59Z',
          rateStatus: 'historical',
        },
        {
          period: '2026-07',
          totalAssets: '11600',
          totalLiabilities: '-1600',
          netWorth: '10000',
          valuedAt: '2026-07-18T02:00:00Z',
          rateStatus: 'current',
        },
      ],
      breakdown: (labels[dimension] ?? labels.root_category).map((item, index) => ({
        ...item,
        income: index === 0 ? '5000' : '1200',
        expense: index === 0 ? '1800' : '600',
        net: index === 0 ? '3200' : '600',
      })),
    });
  });
  void page.route('**/api/v1/exports/transactions.csv**', (route) =>
    route.fulfill({
      status: 200,
      headers: {
        'Content-Type': 'text/csv; charset=utf-8',
        'Content-Disposition': 'attachment; filename="transactions-20260101-20260718.csv"',
        'X-Export-Row-Count': '2',
      },
      body: 'id,amount\r\n1,10\r\n2,20\r\n',
    }),
  );
  return state;
}

async function expectAccessibleAndContained(page: Page): Promise<void> {
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth > document.documentElement.clientWidth,
    ),
  ).toBe(false);
}

async function expectChartsPainted(page: Page): Promise<void> {
  await expect
    .poll(async () => {
      const counts = await page.locator('.report-chart canvas').evaluateAll((canvases) =>
        canvases.map((canvas) => {
          const context = canvas.getContext('2d');
          if (!context) return 0;
          const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
          let painted = 0;
          for (let index = 3; index < pixels.length; index += 4) {
            if (pixels[index] !== 0) painted += 1;
          }
          return painted;
        }),
      );
      return counts.length === 2 && counts.every((count) => count > 100);
    })
    .toBe(true);
}

for (const viewport of [
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`reports filters, charts, tables and export / ${viewport.name}`, async ({ page }) => {
    await page.setViewportSize(viewport);
    const state = installApi(page);
    await page.goto('/reports?startDate=2026-01&endDate=2026-07&dimension=root_category');

    await expect(page.getByRole('heading', { name: 'Reports', exact: true })).toBeVisible();
    await expect(page.getByText('Historical', { exact: true }).first()).toBeVisible();
    await expect(page.getByRole('heading', { name: 'Net worth trend' })).toBeVisible();
    await expect(page.getByRole('heading', { name: 'Root category breakdown' })).toBeVisible();
    await expect(page.getByRole('cell', { name: '10,000' }).first()).toBeVisible();
    await expectChartsPainted(page);
    await expectAccessibleAndContained(page);

    await page.getByRole('combobox', { name: 'Breakdown' }).selectOption('account');
    await page.getByRole('button', { name: 'Apply' }).click();
    await expect(page).toHaveURL(/dimension=account/);
    await expect(page.getByRole('heading', { name: 'Account breakdown' })).toBeVisible();
    await expect(page.getByRole('rowheader', { name: 'Daily Wallet' })).toBeVisible();
    expect(state.dimensions.at(-1)).toBe('account');

    const download = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Export CSV' }).click();
    expect((await download).suggestedFilename()).toBe('transactions-20260101-20260718.csv');
    await expectAccessibleAndContained(page);
  });
}
