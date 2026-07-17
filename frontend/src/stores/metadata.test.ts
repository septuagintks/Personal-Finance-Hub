import { HttpResponse, delay, http as mockHttp } from 'msw';
import { beforeEach, describe, expect, it } from 'vitest';
import { createPinia, setActivePinia } from 'pinia';
import { server } from '../test/server';
import { useMetadataStore } from './metadata';

const category = {
  id: 1,
  name: 'Food',
  board: 'expense' as const,
  source: 'user' as const,
  parentId: null,
  templateId: null,
  sortOrder: 0,
  isDeleted: false,
  deletedAt: null,
  createdAt: '2026-07-18T00:00:00Z',
  updatedAt: '2026-07-18T00:00:00Z',
  children: [],
};

const tag = {
  id: 1,
  name: 'tax',
  isDeleted: false,
  deletedAt: null,
  createdAt: '2026-07-18T00:00:00Z',
  updatedAt: '2026-07-18T00:00:00Z',
};

describe('metadata store', () => {
  beforeEach(() => setActivePinia(createPinia()));

  it('loads active and deleted metadata into the current-user store', async () => {
    server.use(
      mockHttp.get('*/api/v1/categories', ({ request }) => {
        expect(new URL(request.url).searchParams.get('status')).toBe('all');
        return HttpResponse.json([category]);
      }),
      mockHttp.get('*/api/v1/tags', ({ request }) => {
        expect(new URL(request.url).searchParams.get('status')).toBe('all');
        return HttpResponse.json([tag]);
      }),
    );
    const store = useMetadataStore();

    await expect(store.loadCategories()).resolves.toBe(true);
    await expect(store.loadTags()).resolves.toBe(true);
    expect(store.categories[0]?.name).toBe('Food');
    expect(store.tags[0]?.name).toBe('tax');
  });

  it('reloads only the category resource after a successful category rename', async () => {
    let name = 'Food';
    let categoryReads = 0;
    server.use(
      mockHttp.get('*/api/v1/categories', () => {
        categoryReads += 1;
        return HttpResponse.json([{ ...category, name }]);
      }),
      mockHttp.put('*/api/v1/categories/1', async ({ request }) => {
        const body = (await request.json()) as { name: string };
        name = body.name;
        return HttpResponse.json({ ...category, name });
      }),
    );
    const store = useMetadataStore();
    await store.loadCategories();

    await store.updateCategoryItem(1, 'Dining', 10);
    expect(categoryReads).toBe(2);
    expect(store.categories[0]?.name).toBe('Dining');
    expect(store.tagState).toBe('idle');
  });

  it('rejects a late metadata response after the session is cleared', async () => {
    server.use(
      mockHttp.get('*/api/v1/categories', async () => {
        await delay(60);
        return HttpResponse.json([category]);
      }),
    );
    const store = useMetadataStore();
    const pending = store.loadCategories();
    store.clear();

    await expect(pending).resolves.toBe(false);
    expect(store.categories).toEqual([]);
    expect(store.categoryState).toBe('idle');
  });
});
