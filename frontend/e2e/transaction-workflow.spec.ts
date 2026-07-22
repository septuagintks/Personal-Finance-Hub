import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page, type Route } from '@playwright/test';

type Transaction = {
  id: number;
  accountId: number;
  type: 'income' | 'expense' | 'transfer' | 'adjustment';
  amount: string;
  currencyCode: string;
  categoryId: number | null;
  categoryName: string | null;
  categoryDeleted: boolean;
  tags: Array<{ id: number; name: string; isDeleted: boolean }>;
  transferGroupId: number | null;
  correctsTransactionId: number | null;
  correctedByTransactionId: number | null;
  description: string;
  occurredAt: string;
  createdAt: string;
  deletedAt: string | null;
};

const preference = {
  baseCurrency: 'CNY',
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'YYYY-MM-DD',
  numberFormat: '1,234.56',
  theme: 'system',
  defaultHomePage: 'transactions',
  defaultReportPeriod: 'current_month',
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
  createdAt: '2026-07-18T00:00:00Z',
  updatedAt: '2026-07-18T00:00:00Z',
  version: 1,
};

const category = {
  id: 10,
  name: 'Meals',
  board: 'expense',
  source: 'user',
  parentId: null,
  templateId: null,
  sortOrder: 10,
  isDeleted: false,
  deletedAt: null,
  createdAt: '2026-07-18T00:00:00Z',
  updatedAt: '2026-07-18T00:00:00Z',
  children: [],
};

const tag = {
  id: 20,
  name: 'work',
  isDeleted: false,
  deletedAt: null,
  createdAt: '2026-07-18T00:00:00Z',
  updatedAt: '2026-07-18T00:00:00Z',
};

function makeTransaction(id: number, overrides: Partial<Transaction> = {}): Transaction {
  return {
    id,
    accountId: 1,
    type: 'expense',
    amount: String(id * 5),
    currencyCode: 'CNY',
    categoryId: 10,
    categoryName: 'Meals',
    categoryDeleted: false,
    tags: [{ id: 20, name: 'work', isDeleted: false }],
    transferGroupId: null,
    correctsTransactionId: null,
    correctedByTransactionId: null,
    description: `Meal ${id}`,
    occurredAt: `2026-07-${String(10 + id).padStart(2, '0')}T04:00:00Z`,
    createdAt: '2026-07-18T00:00:00Z',
    deletedAt: null,
    ...overrides,
  };
}

async function json(route: Route, body: unknown, status = 200): Promise<void> {
  await route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
}

function installApi(page: Page): { intents: string[]; transactions: Transaction[] } {
  const state = {
    intents: [] as string[],
    transactions: [makeTransaction(1), makeTransaction(2)],
  };
  void page.route('**/api/v1/web/auth/refresh', (route) =>
    json(route, {
      accessToken: 'transaction-access',
      expiresIn: 900,
      tokenType: 'Bearer',
      roles: ['USER'],
    }),
  );
  void page.route('**/api/v1/users/me/preferences', (route) => json(route, preference));
  void page.route('**/api/v1/currencies', (route) =>
    json(route, [
      { code: 'CNY', symbol: '¥', precision: 2, displayName: 'Chinese Yuan', isCrypto: false },
    ]),
  );
  void page.route('**/api/v1/accounts**', (route) => json(route, [account]));
  void page.route('**/api/v1/categories**', (route) => json(route, [category]));
  void page.route('**/api/v1/tags**', (route) => json(route, [tag]));
  void page.route('**/api/v1/transactions**', async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const path = url.pathname;
    const method = request.method();
    if (path === '/api/v1/transactions' && method === 'GET') {
      const active = state.transactions.filter(({ deletedAt }) => deletedAt === null).reverse();
      if (url.searchParams.has('cursor')) {
        await json(route, { items: active.slice(1), nextCursor: null });
      } else {
        await json(route, {
          items: active.slice(0, 1),
          nextCursor: active.length > 1 ? 'page-2' : null,
        });
      }
      return;
    }
    if (path === '/api/v1/transactions' && method === 'POST') {
      state.intents.push(request.headers()['idempotency-key'] ?? '');
      const body = request.postDataJSON() as {
        amount: string;
        description: string | null;
        occurredAt: string | null;
      };
      const created = makeTransaction(3, {
        amount: body.amount.replace(/\.0+$/, ''),
        description: body.description ?? '',
        occurredAt: body.occurredAt ?? '2026-07-18T02:00:00Z',
      });
      state.transactions.push(created);
      await json(route, created, 201);
      return;
    }
    const correction = path.match(/^\/api\/v1\/transactions\/(\d+)\/correction$/);
    if (correction && method === 'POST') {
      state.intents.push(request.headers()['idempotency-key'] ?? '');
      const original = state.transactions.find(({ id }) => id === Number(correction[1]));
      if (!original) return json(route, {}, 404);
      const body = request.postDataJSON() as { amount: string; description: string | null };
      original.deletedAt = '2026-07-18T03:00:00Z';
      original.correctedByTransactionId = 4;
      const replacement = makeTransaction(4, {
        amount: body.amount,
        description: body.description ?? '',
        correctsTransactionId: original.id,
      });
      state.transactions.push(replacement);
      await json(route, replacement, 201);
      return;
    }
    const detail = path.match(/^\/api\/v1\/transactions\/(\d+)$/);
    if (!detail) return route.fallback();
    const found = state.transactions.find(({ id }) => id === Number(detail[1]));
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
  const overflow = await page.evaluate(
    () => document.documentElement.scrollWidth > document.documentElement.clientWidth,
  );
  expect(overflow).toBe(false);
}

for (const viewport of [
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`transaction create, correction, history and delete / ${viewport.name}`, async ({
    page,
  }) => {
    await page.setViewportSize(viewport);
    const state = installApi(page);
    await page.goto('/transactions');

    await expect(page.getByRole('heading', { name: 'Transactions', exact: true })).toBeVisible();
    await expect(page.getByText('Meal 2')).toBeVisible();
    await page.getByRole('button', { name: 'Load more' }).click();
    await expect(page.getByText('Meal 1')).toBeVisible();
    await expectAccessibleAndContained(page);

    if (viewport.name === 'desktop') {
      await page.setViewportSize({ width: 640, height: 360 });
      const results = page.getByRole('region', { name: 'Transaction results' });
      await page.getByRole('button', { name: 'Apply' }).focus();
      await page.keyboard.press('Tab');
      await expect(results).toBeFocused();
      expect(await results.evaluate((element) => element.scrollWidth > element.clientWidth)).toBe(
        true,
      );
      expect(
        await results.evaluate((element) => {
          const style = getComputedStyle(element);
          return style.outlineStyle !== 'none' && Number.parseFloat(style.outlineWidth) >= 1;
        }),
      ).toBe(true);
      await page.keyboard.press('ArrowRight');
      await expect.poll(() => results.evaluate((element) => element.scrollLeft)).toBeGreaterThan(0);
      await page.setViewportSize(viewport);
    }

    await page.getByRole('button', { name: 'Record transaction' }).click();
    const create = page.getByRole('dialog', { name: 'Record transaction' });
    await create.getByLabel('Amount', { exact: true }).fill('18.50');
    await create.getByLabel('Category').selectOption('10');
    await create.getByLabel('Description').fill('Team lunch');
    await create.getByLabel('work').check();
    await create.getByRole('button', { name: 'Record', exact: true }).click();

    await expect(page).toHaveURL(/\/transactions\/3$/);
    await expect(page.getByRole('heading', { name: 'Team lunch' })).toBeVisible();
    await expectAccessibleAndContained(page);

    await page.getByRole('button', { name: 'Correct' }).click();
    const correction = page.getByRole('dialog', { name: 'Correct transaction' });
    await correction.getByLabel('Amount', { exact: true }).fill('19.25');
    await correction.getByRole('button', { name: 'Create correction' }).click();

    await expect(page).toHaveURL(/\/transactions\/4$/);
    await expect(page.getByText('19.25')).toBeVisible();
    await page.getByRole('link', { name: 'Original transaction 3' }).click();
    await expect(page.getByText('This transaction is no longer active.')).toBeVisible();
    await page.getByRole('link', { name: 'Replacement transaction 4' }).click();

    await page.getByRole('button', { name: 'Delete' }).click();
    await page.getByRole('button', { name: 'Delete transaction' }).click();
    await expect(page).toHaveURL(/\/transactions$/);
    expect(state.intents).toHaveLength(2);
    expect(state.intents.every(Boolean)).toBe(true);
    expect(new Set(state.intents).size).toBe(2);
    await expectAccessibleAndContained(page);
  });
}
