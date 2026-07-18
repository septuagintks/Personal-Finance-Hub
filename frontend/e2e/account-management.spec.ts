import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page, type Route } from '@playwright/test';

type Account = {
  id: number;
  name: string;
  type: string;
  subtype: string;
  category: 'asset' | 'liability';
  currencyCode: string;
  description: string;
  isArchived: boolean;
  archivedAt: string | null;
  createdAt: string;
  updatedAt: string;
  version: number;
};

const preference = {
  baseCurrency: 'CNY',
  locale: 'en-US',
  timezone: 'Asia/Shanghai',
  dateFormat: 'yyyy-MM-dd',
  numberFormat: 'standard',
  theme: 'system',
  defaultHomePage: 'accounts',
  defaultReportPeriod: 'current_month',
};

const currencies = [
  { code: 'CNY', symbol: '¥', precision: 2, displayName: 'Chinese Yuan', isCrypto: false },
  { code: 'USD', symbol: '$', precision: 2, displayName: 'US Dollar', isCrypto: false },
];

const longAccountName =
  'InternationalTravelReserveAndReconciliationAccountForAnnualFinancialReporting';

function makeAccount(id: number, overrides: Partial<Account> = {}): Account {
  return {
    id,
    name: `Account ${id}`,
    type: 'cash',
    subtype: 'wallet',
    category: 'asset',
    currencyCode: 'CNY',
    description: '',
    isArchived: false,
    archivedAt: null,
    createdAt: '2026-07-16T00:00:00Z',
    updatedAt: '2026-07-16T00:00:00Z',
    version: 1,
    ...overrides,
  };
}

async function fulfillJson(
  route: Route,
  body: unknown,
  status = 200,
  headers: Record<string, string> = {},
): Promise<void> {
  await route.fulfill({
    status,
    contentType: 'application/json',
    headers,
    body: JSON.stringify(body),
  });
}

async function installSession(page: Page): Promise<void> {
  await page.route('**/api/v1/web/auth/refresh', (route) =>
    fulfillJson(route, {
      accessToken: 'accounts-e2e-access',
      expiresIn: 900,
      tokenType: 'Bearer',
      roles: ['USER'],
    }),
  );
  await page.route('**/api/v1/users/me/preferences', (route) => fulfillJson(route, preference));
  await page.route('**/api/v1/currencies', (route) => fulfillJson(route, currencies));
}

async function expectAccessible(page: Page): Promise<void> {
  const result = await new AxeBuilder({ page }).analyze();
  expect(result.violations).toEqual([]);
}

async function expectNoHorizontalOverflow(page: Page): Promise<void> {
  const overflow = await page.evaluate(
    () => document.documentElement.scrollWidth > document.documentElement.clientWidth,
  );
  expect(overflow).toBe(false);
}

function installAccountApi(
  page: Page,
  options: { conflictOnUpdate?: boolean } = {},
): {
  accounts: Account[];
  validators: string[];
  deleteConfirmations: string[];
} {
  const state = {
    accounts: [
      makeAccount(1, { name: 'Everyday Cash' }),
      makeAccount(4, { name: longAccountName, currencyCode: 'USD' }),
      makeAccount(2, {
        name: 'Old Wallet',
        isArchived: true,
        archivedAt: '2026-07-10T00:00:00Z',
        version: 2,
      }),
    ],
    validators: [] as string[],
    deleteConfirmations: [] as string[],
  };

  void page.route('**/api/v1/accounts**', async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const path = url.pathname;
    const method = request.method();

    if (path === '/api/v1/accounts' && method === 'GET') {
      const status = url.searchParams.get('status') ?? 'active';
      const selected = state.accounts.filter(
        (account) =>
          status === 'all' ||
          (status === 'active' && !account.isArchived) ||
          (status === 'archived' && account.isArchived),
      );
      await fulfillJson(route, selected);
      return;
    }

    if (path === '/api/v1/accounts' && method === 'POST') {
      const payload = request.postDataJSON() as Omit<
        Account,
        'id' | 'isArchived' | 'archivedAt' | 'createdAt' | 'updatedAt' | 'version'
      >;
      const created = makeAccount(3, payload);
      state.accounts.push(created);
      await fulfillJson(route, created, 201);
      return;
    }

    const matched = path.match(/^\/api\/v1\/accounts\/(\d+)(?:\/(balance|archive|restore))?$/);
    if (!matched) {
      await route.fallback();
      return;
    }
    const id = Number(matched[1]);
    const action = matched[2] ?? '';
    const index = state.accounts.findIndex((account) => account.id === id);
    if (index < 0) {
      await fulfillJson(
        route,
        {
          error_code: 'NOT_FOUND',
          message: 'Account not found.',
          trace_id: 'e2e-account-not-found',
          retryable: false,
          field_errors: [],
        },
        404,
      );
      return;
    }
    const current = state.accounts[index];

    if (action === 'balance' && method === 'GET') {
      await fulfillJson(route, {
        accountId: id,
        currencyCode: current.currencyCode,
        balance: id === 3 ? '458.75000000' : '100.00000000',
        lastTransactionId: null,
        updatedAt: '2026-07-16T02:00:00Z',
      });
      return;
    }

    if (!action && method === 'GET') {
      await fulfillJson(route, current, 200, { ETag: `"${current.version}"` });
      return;
    }

    if (!action && method === 'PUT') {
      state.validators.push(request.headers()['if-match'] ?? '');
      if (options.conflictOnUpdate) {
        await fulfillJson(
          route,
          {
            error_code: 'CONFLICT',
            message: 'Account version conflict.',
            trace_id: 'e2e-account-conflict',
            retryable: false,
            field_errors: [],
          },
          409,
        );
        return;
      }
      const payload = request.postDataJSON() as Partial<Account>;
      const updated = {
        ...current,
        ...payload,
        version: current.version + 1,
        updatedAt: '2026-07-16T03:00:00Z',
      };
      state.accounts.splice(index, 1, updated);
      await fulfillJson(route, updated, 200, { ETag: `"${updated.version}"` });
      return;
    }

    if ((action === 'archive' || action === 'restore') && method === 'POST') {
      state.validators.push(request.headers()['if-match'] ?? '');
      const next = {
        ...current,
        isArchived: action === 'archive',
        archivedAt: action === 'archive' ? '2026-07-16T04:00:00Z' : null,
        updatedAt: '2026-07-16T04:00:00Z',
        version: current.version + 1,
      };
      state.accounts.splice(index, 1, next);
      await route.fulfill({ status: 204 });
      return;
    }

    if (!action && method === 'DELETE') {
      state.deleteConfirmations.push(url.searchParams.get('confirmations') ?? '');
      state.accounts.splice(index, 1);
      await route.fulfill({ status: 204 });
      return;
    }

    await route.fallback();
  });

  return state;
}

for (const viewport of [
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test.describe(`account management / ${viewport.name}`, () => {
    test.beforeEach(async ({ page }) => {
      await page.setViewportSize({ width: viewport.width, height: viewport.height });
      await installSession(page);
    });

    test('completes create, edit, archive, restore, and three-step delete', async ({ page }) => {
      const state = installAccountApi(page);
      await page.goto('/accounts');

      await expect(page.getByRole('heading', { name: 'Accounts', exact: true })).toBeVisible();
      await expect(page.getByText('Everyday Cash')).toBeVisible();
      await expect(page.getByText(longAccountName)).toBeVisible();
      await page.getByRole('button', { name: 'Archived' }).click();
      await expect(page.getByText('Old Wallet')).toBeVisible();
      await page.getByRole('button', { name: 'Active' }).click();
      await expectAccessible(page);
      await expectNoHorizontalOverflow(page);

      await page.getByRole('button', { name: 'New account' }).click();
      const createDialog = page.getByRole('dialog', { name: 'Create account' });
      await createDialog.getByLabel('Name', { exact: true }).fill('Travel Wallet');
      await createDialog.getByRole('combobox', { name: /^Type/ }).selectOption('digital_wallet');
      await createDialog.getByLabel('Subtype', { exact: true }).fill('mobile');
      await createDialog.getByRole('combobox', { name: /^Classification/ }).selectOption('asset');
      await createDialog.getByRole('combobox', { name: /^Currency/ }).selectOption('USD');
      await createDialog.getByLabel('Description', { exact: true }).fill('Trips and travel');
      await createDialog.getByRole('button', { name: 'Create account', exact: true }).click();

      await expect(page).toHaveURL(/\/accounts\/3$/);
      await expect(page.getByRole('heading', { name: 'Travel Wallet' })).toBeVisible();
      await expect(page.getByText('458.75')).toBeVisible();
      await expectAccessible(page);
      await expectNoHorizontalOverflow(page);

      await page.getByRole('button', { name: 'Edit' }).click();
      const editDialog = page.getByRole('dialog', { name: 'Edit account' });
      await editDialog.getByLabel('Name', { exact: true }).fill('Travel Reserve');
      await editDialog.getByRole('button', { name: 'Save changes' }).click();
      await expect(page.getByRole('heading', { name: 'Travel Reserve' })).toBeVisible();

      await page.getByRole('button', { name: 'Archive', exact: true }).click();
      await page.getByRole('button', { name: 'Archive account', exact: true }).click();
      await expect(page.getByText(/cannot receive transactions or transfers/)).toBeVisible();

      await page.getByRole('button', { name: 'Restore', exact: true }).click();
      await page.getByRole('button', { name: 'Restore account', exact: true }).click();
      await expect(page.getByRole('button', { name: 'Archive', exact: true })).toBeVisible();

      await page.getByRole('button', { name: 'Delete account' }).click();
      await expect(page.getByRole('heading', { name: 'Review affected records' })).toBeVisible();
      await page.getByRole('button', { name: 'Continue' }).click();
      await page.getByRole('checkbox').check();
      await page.getByRole('button', { name: 'Continue' }).click();
      await page.locator('input[name="delete-account-name"]').fill('Travel Reserve');
      await page.getByRole('button', { name: 'Delete permanently' }).click();

      await expect(page).toHaveURL(/\/accounts$/);
      await expect(page.getByText('Everyday Cash')).toBeVisible();
      await expect(page.getByText('Travel Reserve')).toHaveCount(0);
      expect(state.validators).toEqual(['"1"', '"2"', '"3"']);
      expect(state.deleteConfirmations).toEqual(['3']);
      await expectNoHorizontalOverflow(page);
    });

    test('keeps a conflicting edit visible and recoverable', async ({ page }) => {
      installAccountApi(page, { conflictOnUpdate: true });
      await page.goto('/accounts/1');
      await expect(page.getByRole('heading', { name: 'Everyday Cash' })).toBeVisible();

      await page.getByRole('button', { name: 'Edit' }).click();
      const editDialog = page.getByRole('dialog', { name: 'Edit account' });
      await editDialog.getByLabel('Name', { exact: true }).fill('Changed elsewhere');
      await editDialog.getByRole('button', { name: 'Save changes' }).click();
      await expect(page.getByRole('alert')).toContainText('changed elsewhere');
      await expect(page.getByRole('dialog')).toBeVisible();
      await expectAccessible(page);
    });
  });
}
