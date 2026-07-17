import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page, type Route } from '@playwright/test';

const currencies = [
  { code: 'CNY', symbol: '¥', precision: 2, displayName: 'Chinese Yuan', isCrypto: false },
  { code: 'USD', symbol: '$', precision: 2, displayName: 'US Dollar', isCrypto: false },
];

let preference = {
  baseCurrency: 'CNY',
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'YYYY-MM-DD',
  numberFormat: '1,234.56',
  theme: 'system',
  defaultHomePage: 'dashboard',
  defaultReportPeriod: 'current_month',
};

async function json(route: Route, body: unknown, status = 200): Promise<void> {
  await route.fulfill({ status, contentType: 'application/json', body: JSON.stringify(body) });
}

function resource(id: number, name: string, board: 'income' | 'expense' = 'expense') {
  return {
    id,
    name,
    board,
    source: 'user',
    parentId: null,
    templateId: null,
    sortOrder: id * 10,
    isDeleted: false,
    deletedAt: null as string | null,
    createdAt: '2026-07-18T00:00:00Z',
    updatedAt: '2026-07-18T00:00:00Z',
    children: [],
  };
}

function installApi(page: Page): void {
  const categories = [resource(1, 'Food')];
  const tags = [
    {
      id: 1,
      name: 'tax',
      isDeleted: false,
      deletedAt: null as string | null,
      createdAt: '2026-07-18T00:00:00Z',
      updatedAt: '2026-07-18T00:00:00Z',
    },
  ];

  void page.route('**/api/v1/web/auth/refresh', (route) =>
    json(route, { accessToken: 'settings-access', expiresIn: 900, tokenType: 'Bearer' }),
  );
  void page.route('**/api/v1/currencies', (route) => json(route, currencies));
  void page.route('**/api/v1/users/me/preferences', async (route) => {
    if (route.request().method() === 'PUT') {
      preference = route.request().postDataJSON() as typeof preference;
    }
    await json(route, preference);
  });
  void page.route('**/api/v1/categories**', async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    const method = request.method();
    if (path === '/api/v1/categories' && method === 'GET') {
      await json(route, categories);
      return;
    }
    if (path === '/api/v1/categories' && method === 'POST') {
      const body = request.postDataJSON() as { name: string; board: 'income' | 'expense' };
      const created = resource(2, body.name, body.board);
      categories.push(created);
      await json(route, created, 201);
      return;
    }
    const matched = path.match(/^\/api\/v1\/categories\/(\d+)(\/restore)?$/);
    if (!matched) return route.fallback();
    const item = categories.find(({ id }) => id === Number(matched[1]));
    if (!item) return json(route, {}, 404);
    if (matched[2] && method === 'POST') {
      item.isDeleted = false;
      item.deletedAt = null;
      await json(route, item);
      return;
    }
    if (method === 'PUT') {
      const body = request.postDataJSON() as { name: string; sortOrder: number };
      item.name = body.name;
      item.sortOrder = body.sortOrder;
      await json(route, item);
      return;
    }
    if (method === 'DELETE') {
      item.isDeleted = true;
      item.deletedAt = '2026-07-18T01:00:00Z';
      await route.fulfill({ status: 204 });
      return;
    }
    await route.fallback();
  });
  void page.route('**/api/v1/tags**', async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    const method = request.method();
    if (path === '/api/v1/tags' && method === 'GET') return json(route, tags);
    if (path === '/api/v1/tags' && method === 'POST') {
      const body = request.postDataJSON() as { name: string };
      const created = { ...tags[0], id: 2, name: body.name };
      tags.push(created);
      return json(route, created, 201);
    }
    const matched = path.match(/^\/api\/v1\/tags\/(\d+)(\/restore)?$/);
    if (!matched) return route.fallback();
    const item = tags.find(({ id }) => id === Number(matched[1]));
    if (!item) return json(route, {}, 404);
    if (matched[2] && method === 'POST') {
      item.isDeleted = false;
      item.deletedAt = null;
      return json(route, item);
    }
    if (method === 'PUT') {
      item.name = (request.postDataJSON() as { name: string }).name;
      return json(route, item);
    }
    if (method === 'DELETE') {
      item.isDeleted = true;
      item.deletedAt = '2026-07-18T01:00:00Z';
      return route.fulfill({ status: 204 });
    }
    await route.fallback();
  });
}

for (const viewport of [
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`settings lifecycle / ${viewport.name}`, async ({ page }) => {
    preference = { ...preference, locale: 'en-US', theme: 'system' };
    await page.setViewportSize(viewport);
    installApi(page);
    await page.goto('/settings');

    await expect(page.getByRole('heading', { name: 'Settings' })).toBeVisible();
    await page.getByRole('button', { name: 'New category' }).click();
    const categoryDialog = page.getByRole('dialog', { name: 'New category' });
    await categoryDialog.getByLabel('Name').fill('Travel');
    await categoryDialog.getByRole('button', { name: 'Save' }).click();
    const categoryRow = page.locator('.settings-row').filter({ hasText: 'Travel' });
    await expect(categoryRow).toBeVisible();
    await categoryRow.getByTitle('Edit').click();
    const editDialog = page.getByRole('dialog', { name: 'Edit category' });
    await editDialog.getByLabel('Name').fill('Trips');
    await editDialog.getByRole('button', { name: 'Save' }).click();
    const tripsRow = page.locator('.settings-row').filter({ hasText: 'Trips' });
    await tripsRow.getByTitle('Delete').click();
    await page.getByLabel('Show deleted').check();
    await expect(tripsRow).toContainText('Deleted');
    await tripsRow.getByTitle('Restore').click();
    await expect(tripsRow).toContainText('Active');

    await page.getByRole('tab', { name: 'Tags' }).click();
    await page.getByRole('button', { name: 'New tag' }).click();
    const tagDialog = page.getByRole('dialog', { name: 'New tag' });
    await tagDialog.getByLabel('Name').fill('vacation');
    await tagDialog.getByRole('button', { name: 'Save' }).click();
    await expect(page.locator('.settings-row').filter({ hasText: 'vacation' })).toBeVisible();

    await page.getByRole('tab', { name: 'Preferences' }).click();
    await page.getByLabel('Language').selectOption('zh-CN');
    await page.getByLabel('Theme').selectOption('dark');
    await page.getByRole('button', { name: 'Save preferences' }).click();
    await expect(page.getByRole('heading', { name: '设置' })).toBeVisible();
    await expect(page.locator('html')).toHaveAttribute('lang', 'zh-CN');
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');

    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth > document.documentElement.clientWidth,
    );
    expect(overflow).toBe(false);
    expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  });
}
