import { HttpResponse, http as mockHttp } from 'msw';
import { describe, expect, it } from 'vitest';
import { server } from '../test/server';
import { createCategory, createTag } from './metadata-api';

describe('metadata API contract', () => {
  it('forwards the caller-owned category and tag intent keys', async () => {
    const keys: string[] = [];
    server.use(
      mockHttp.post('*/api/v1/categories', ({ request }) => {
        keys.push(request.headers.get('idempotency-key') ?? '');
        return HttpResponse.json(
          {
            id: 2,
            name: 'Food',
            board: 'expense',
            source: 'user',
            parentId: null,
            templateId: null,
            sortOrder: 0,
            isDeleted: false,
            deletedAt: null,
            createdAt: '2026-07-18T00:00:00Z',
            updatedAt: '2026-07-18T00:00:00Z',
          },
          { status: 201 },
        );
      }),
      mockHttp.post('*/api/v1/tags', ({ request }) => {
        keys.push(request.headers.get('idempotency-key') ?? '');
        return HttpResponse.json(
          {
            id: 3,
            name: 'tax',
            isDeleted: false,
            deletedAt: null,
            createdAt: '2026-07-18T00:00:00Z',
            updatedAt: '2026-07-18T00:00:00Z',
          },
          { status: 201 },
        );
      }),
    );

    await createCategory(
      { name: 'Food', board: 'expense', parentId: null },
      'category-stable-intent',
    );
    await createTag({ name: 'tax' }, 'tag-stable-intent');

    expect(keys).toEqual(['category-stable-intent', 'tag-stable-intent']);
  });
});
