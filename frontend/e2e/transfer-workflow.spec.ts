import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page, type Route } from '@playwright/test';

type Transfer = {
  transferGroupId: number;
  mode: 'OutgoingAndRate' | 'BothAmounts' | 'IncomingAndRate';
  sourceAccountId: number;
  targetAccountId: number;
  outgoingTransactionId: number;
  incomingTransactionId: number;
  adjustmentTransactionIds: number[];
  outgoingAmount: string;
  incomingAmount: string;
  sourceCurrencyCode: string;
  targetCurrencyCode: string;
  rate: string | null;
  feeAmount: string | null;
  feeSource: 'SourceAccount' | 'TargetAccount' | 'ThirdParty' | null;
  feeAccountId: number | null;
  feeCurrencyCode: string | null;
  description: string;
  occurredAt: string;
  createdAt: string;
  deletedAt: string | null;
  correctsTransferGroupId: number | null;
  correctedByTransferGroupId: number | null;
};

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

const accounts = [
  { id: 1, name: 'CNY Wallet', currencyCode: 'CNY' },
  { id: 2, name: 'USD Savings', currencyCode: 'USD' },
  { id: 3, name: 'HKD Fee Account', currencyCode: 'HKD' },
].map((account) => ({
  ...account,
  type: 'savings',
  subtype: 'bank',
  category: 'asset',
  description: '',
  isArchived: false,
  archivedAt: null,
  createdAt: '2026-07-18T00:00:00Z',
  updatedAt: '2026-07-18T00:00:00Z',
  version: 1,
}));

function transfer(id: number, overrides: Partial<Transfer> = {}): Transfer {
  return {
    transferGroupId: id,
    mode: 'BothAmounts',
    sourceAccountId: 1,
    targetAccountId: 2,
    outgoingTransactionId: id * 10,
    incomingTransactionId: id * 10 + 1,
    adjustmentTransactionIds: [id * 10 + 2],
    outgoingAmount: '70',
    incomingAmount: '10',
    sourceCurrencyCode: 'CNY',
    targetCurrencyCode: 'USD',
    rate: '0.1428571429',
    feeAmount: '1',
    feeSource: 'ThirdParty',
    feeAccountId: 3,
    feeCurrencyCode: 'HKD',
    description: `Transfer ${id}`,
    occurredAt: '2026-07-18T02:00:00Z',
    createdAt: '2026-07-18T02:00:01Z',
    deletedAt: null,
    correctsTransferGroupId: null,
    correctedByTransferGroupId: null,
    ...overrides,
  };
}

async function json(route: Route, body: unknown, status = 200): Promise<void> {
  await route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
}

function installApi(page: Page): { intents: string[]; transfers: Transfer[] } {
  const state = { intents: [] as string[], transfers: [transfer(1)] };
  void page.route('**/api/v1/web/auth/refresh', (route) =>
    json(route, {
      accessToken: 'transfer-access',
      expiresIn: 900,
      tokenType: 'Bearer',
      roles: ['USER'],
    }),
  );
  void page.route('**/api/v1/users/me/preferences', (route) => json(route, preference));
  void page.route('**/api/v1/currencies', (route) =>
    json(
      route,
      accounts.map(({ currencyCode }) => ({
        code: currencyCode,
        symbol: currencyCode,
        precision: 2,
        displayName: currencyCode,
        isCrypto: false,
      })),
    ),
  );
  void page.route('**/api/v1/accounts**', (route) => json(route, accounts));
  void page.route('**/api/v1/transfers**', async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const path = url.pathname;
    const method = request.method();
    if (path === '/api/v1/transfers' && method === 'GET') {
      await json(route, {
        items: state.transfers.filter(({ deletedAt }) => deletedAt === null).reverse(),
        nextCursor: null,
      });
      return;
    }
    if (path === '/api/v1/transfers' && method === 'POST') {
      state.intents.push(request.headers()['idempotency-key'] ?? '');
      const body = request.postDataJSON() as {
        outgoingAmount: string;
        incomingAmount: string;
        feeAmount: string;
        description: string;
      };
      const created = transfer(2, {
        outgoingAmount: body.outgoingAmount,
        incomingAmount: body.incomingAmount,
        feeAmount: body.feeAmount,
        description: body.description,
      });
      state.transfers.push(created);
      await json(route, created, 201);
      return;
    }
    const correction = path.match(/^\/api\/v1\/transfers\/(\d+)\/correction$/);
    if (correction && method === 'POST') {
      state.intents.push(request.headers()['idempotency-key'] ?? '');
      const original = state.transfers.find(
        ({ transferGroupId }) => transferGroupId === Number(correction[1]),
      );
      if (!original) return json(route, {}, 404);
      original.deletedAt = '2026-07-18T03:00:00Z';
      original.correctedByTransferGroupId = 3;
      const body = request.postDataJSON() as {
        incomingAmount: string;
        rate: string;
        description: string;
      };
      const replacement = transfer(3, {
        mode: 'IncomingAndRate',
        outgoingAmount: '72',
        incomingAmount: body.incomingAmount,
        rate: body.rate,
        description: body.description,
        correctsTransferGroupId: original.transferGroupId,
      });
      state.transfers.push(replacement);
      await json(route, replacement, 201);
      return;
    }
    const detail = path.match(/^\/api\/v1\/transfers\/(\d+)$/);
    if (!detail) return route.fallback();
    const found = state.transfers.find(
      ({ transferGroupId }) => transferGroupId === Number(detail[1]),
    );
    if (!found) return json(route, {}, 404);
    if (method === 'GET') return json(route, found);
    if (method === 'DELETE') {
      found.deletedAt = '2026-07-18T04:00:00Z';
      await route.fulfill({ status: 204 });
      return;
    }
    await route.fallback();
  });
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

for (const viewport of [
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`transfer create, correction, history and delete / ${viewport.name}`, async ({ page }) => {
    await page.setViewportSize(viewport);
    const state = installApi(page);
    await page.goto('/transfers');

    await expect(page.getByRole('heading', { name: 'Transfers', exact: true })).toBeVisible();
    await expect(page.getByText('Transfer 1')).toBeVisible();
    await expectAccessibleAndContained(page);

    await page.getByRole('button', { name: 'Record transfer' }).click();
    const create = page.getByRole('dialog', { name: 'Record transfer' });
    await create.getByLabel(/Outgoing amount/).fill('720');
    await create.getByLabel(/Incoming amount/).fill('100');
    await expect(create.getByText('Derived rate')).toBeVisible();
    await create.getByLabel('Fee source').selectOption('ThirdParty');
    await create.locator('select[name="transfer-fee-account"]').selectOption('3');
    await create.getByLabel(/Fee amount/).fill('3');
    await create.getByLabel('Description').fill('International settlement');
    await create.getByRole('button', { name: 'Record transfer', exact: true }).click();

    await expect(page).toHaveURL(/\/transfers\/2$/);
    await expect(page.getByRole('heading', { name: 'International settlement' })).toBeVisible();
    await expect(page.locator('dt', { hasText: 'Fee account' }).locator('+ dd')).toHaveText(
      'HKD Fee Account',
    );
    await expectAccessibleAndContained(page);

    await page.getByRole('button', { name: 'Correct' }).click();
    const correction = page.getByRole('dialog', { name: 'Correct transfer' });
    await correction.getByRole('button', { name: 'Receive + rate' }).click();
    await correction.getByLabel(/Incoming amount/).fill('101');
    await correction.getByLabel('Exchange rate').fill('1.4');
    await correction.getByLabel('Description').fill('Corrected settlement');
    await correction.getByRole('button', { name: 'Create correction' }).click();

    await expect(page).toHaveURL(/\/transfers\/3$/);
    await expect(page.getByRole('heading', { name: 'Corrected settlement' })).toBeVisible();
    await page.getByRole('link', { name: 'Original transfer 2' }).click();
    await expect(page.getByText('This transfer is no longer active.')).toBeVisible();
    await page.getByRole('link', { name: 'Replacement transfer 3' }).click();

    await page.getByRole('button', { name: 'Delete' }).click();
    const deleteDialog = page.getByRole('dialog', { name: 'Delete transfer' });
    await expect(deleteDialog.getByText('CNY Wallet')).toBeVisible();
    await expect(deleteDialog.getByText('USD Savings')).toBeVisible();
    await deleteDialog.getByRole('button', { name: 'Delete transfer' }).click();
    await expect(page).toHaveURL(/\/transfers$/);
    expect(state.intents).toHaveLength(2);
    expect(state.intents.every(Boolean)).toBe(true);
    expect(new Set(state.intents).size).toBe(2);
    await expectAccessibleAndContained(page);
  });
}
