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
    await delay(70);
    expect(store.categories).toEqual([]);
    expect(store.categoryState).toBe('idle');
  });

  it('serializes category and tag writes through one FIFO queue', async () => {
    const writes: string[] = [];
    let categoryName = category.name;
    let tagName = tag.name;
    server.use(
      mockHttp.get('*/api/v1/categories', () =>
        HttpResponse.json([{ ...category, name: categoryName }]),
      ),
      mockHttp.get('*/api/v1/tags', () => HttpResponse.json([{ ...tag, name: tagName }])),
      mockHttp.post('*/api/v1/categories', async ({ request }) => {
        writes.push('category:start');
        await delay(30);
        categoryName = ((await request.json()) as { name: string }).name;
        writes.push('category:end');
        return HttpResponse.json({ ...category, id: 2, name: categoryName }, { status: 201 });
      }),
      mockHttp.post('*/api/v1/tags', async ({ request }) => {
        writes.push('tag:start');
        tagName = ((await request.json()) as { name: string }).name;
        writes.push('tag:end');
        return HttpResponse.json({ ...tag, id: 2, name: tagName }, { status: 201 });
      }),
    );
    const store = useMetadataStore();

    const categoryWrite = store.createCategoryItem({
      board: 'expense',
      name: 'Dining',
      parentId: null,
    });
    const tagWrite = store.createTagItem('travel');

    expect(store.mutationPending).toBe(true);
    await expect(Promise.all([categoryWrite, tagWrite])).resolves.toHaveLength(2);
    expect(writes).toEqual(['category:start', 'category:end', 'tag:start', 'tag:end']);
    expect(store.categories[0]?.name).toBe('Dining');
    expect(store.tags[0]?.name).toBe('travel');
    expect(store.mutationPending).toBe(false);
  });

  it('keeps the first committed write and reloads after the next queued write fails', async () => {
    const writes: string[] = [];
    let categoryName = category.name;
    server.use(
      mockHttp.put('*/api/v1/categories/1', async ({ request }) => {
        writes.push('category');
        categoryName = ((await request.json()) as { name: string }).name;
        return HttpResponse.json({ ...category, name: categoryName });
      }),
      mockHttp.get('*/api/v1/categories', () =>
        HttpResponse.json([{ ...category, name: categoryName }]),
      ),
      mockHttp.put('*/api/v1/tags/1', () => {
        writes.push('tag');
        return HttpResponse.json(
          { error_code: 'CONFLICT', message: 'conflict', field_errors: [] },
          { status: 409 },
        );
      }),
      mockHttp.get('*/api/v1/tags', () => HttpResponse.json([tag])),
    );
    const store = useMetadataStore();

    const results = await Promise.allSettled([
      store.updateCategoryItem(1, 'Dining', 10),
      store.updateTagItem(1, 'travel'),
    ]);

    expect(results.map(({ status }) => status)).toEqual(['fulfilled', 'rejected']);
    expect(writes).toEqual(['category', 'tag']);
    expect(store.categories[0]?.name).toBe('Dining');
    expect(store.tags[0]?.name).toBe('tax');
    expect(store.categoryState).toBe('ready');
    expect(store.tagState).toBe('ready');
    expect(store.mutationPending).toBe(false);
  });

  it('aborts the current write and rejects unsent writes when the session is cleared', async () => {
    let categoryWrites = 0;
    let tagWrites = 0;
    let currentTag = tag;
    server.use(
      mockHttp.post('*/api/v1/categories', async () => {
        categoryWrites += 1;
        await delay(60);
        return HttpResponse.json({ ...category, id: 2 }, { status: 201 });
      }),
      mockHttp.post('*/api/v1/tags', async ({ request }) => {
        tagWrites += 1;
        currentTag = {
          ...tag,
          id: 2,
          name: ((await request.json()) as { name: string }).name,
        };
        return HttpResponse.json(currentTag, { status: 201 });
      }),
      mockHttp.get('*/api/v1/tags', () => HttpResponse.json([currentTag])),
    );
    const store = useMetadataStore();
    const current = store.createCategoryItem({
      board: 'expense',
      name: 'Dining',
      parentId: null,
    });
    const queued = store.createTagItem('travel');

    await delay(10);
    store.clear();
    const nextSession = store.createTagItem('new-user');
    const results = await Promise.allSettled([current, queued, nextSession]);
    await delay(70);

    expect(results.map(({ status }) => status)).toEqual(['rejected', 'rejected', 'fulfilled']);
    expect(categoryWrites).toBe(1);
    expect(tagWrites).toBe(1);
    expect(store.categories).toEqual([]);
    expect(store.tags[0]?.name).toBe('new-user');
    expect(store.mutationPending).toBe(false);
  });

  it('reuses a create idempotency key after an uncertain network result', async () => {
    const keys: string[] = [];
    let attempts = 0;
    let created = false;
    server.use(
      mockHttp.post('*/api/v1/categories', ({ request }) => {
        attempts += 1;
        keys.push(request.headers.get('Idempotency-Key') ?? '');
        if (attempts === 1) return HttpResponse.error();
        created = true;
        return HttpResponse.json({ ...category, id: 2, name: 'Dining' }, { status: 201 });
      }),
      mockHttp.get('*/api/v1/categories', () =>
        HttpResponse.json(created ? [{ ...category, id: 2, name: 'Dining' }] : [category]),
      ),
    );
    const store = useMetadataStore();
    const payload = { board: 'expense' as const, name: 'Dining', parentId: null };

    await expect(store.createCategoryItem(payload)).rejects.toBeDefined();
    await expect(store.createCategoryItem(payload)).resolves.toMatchObject({ name: 'Dining' });

    expect(keys).toHaveLength(2);
    expect(keys[0]).toBeTruthy();
    expect(keys[1]).toBe(keys[0]);
    expect(store.categories[0]?.name).toBe('Dining');
    expect(store.mutationPending).toBe(false);
  });
});
