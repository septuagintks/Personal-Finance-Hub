import type { components } from '../generated/api-types';
import { http } from './http';
import { newIntentKey } from './idempotency';

export type Category = components['schemas']['Category'];
export type CategoryTree = components['schemas']['CategoryTree'];
export type CreateCategoryRequest = components['schemas']['CreateCategoryRequest'];
export type UpdateCategoryRequest = components['schemas']['UpdateCategoryRequest'];
export type Tag = components['schemas']['Tag'];
export type CreateTagRequest = components['schemas']['CreateTagRequest'];
export type UpdateTagRequest = components['schemas']['UpdateTagRequest'];
export type MetadataStatus = components['schemas']['MetadataStatus'];
export type CategoryBoard = components['schemas']['CategoryBoard'];

export async function listCategories(
  status: MetadataStatus = 'active',
  signal?: AbortSignal,
): Promise<CategoryTree[]> {
  const response = await http.get<CategoryTree[]>('/api/v1/categories', {
    params: { status },
    signal,
  });
  return response.data;
}

export async function createCategory(
  payload: CreateCategoryRequest,
  signal?: AbortSignal,
): Promise<Category> {
  const response = await http.post<Category>('/api/v1/categories', payload, {
    headers: { 'Idempotency-Key': newIntentKey('category') },
    signal,
  });
  return response.data;
}

export async function updateCategory(
  categoryId: number,
  payload: UpdateCategoryRequest,
  signal?: AbortSignal,
): Promise<Category> {
  const response = await http.put<Category>(`/api/v1/categories/${categoryId}`, payload, {
    _pfhReplayAfterRefresh: true,
    signal,
  });
  return response.data;
}

export async function deleteCategory(categoryId: number, signal?: AbortSignal): Promise<void> {
  await http.delete(`/api/v1/categories/${categoryId}`, { signal });
}

export async function restoreCategory(categoryId: number, signal?: AbortSignal): Promise<Category> {
  const response = await http.post<Category>(
    `/api/v1/categories/${categoryId}/restore`,
    undefined,
    { _pfhReplayAfterRefresh: true, signal },
  );
  return response.data;
}

export async function listTags(
  status: MetadataStatus = 'active',
  signal?: AbortSignal,
): Promise<Tag[]> {
  const response = await http.get<Tag[]>('/api/v1/tags', {
    params: { status },
    signal,
  });
  return response.data;
}

export async function createTag(payload: CreateTagRequest, signal?: AbortSignal): Promise<Tag> {
  const response = await http.post<Tag>('/api/v1/tags', payload, {
    headers: { 'Idempotency-Key': newIntentKey('tag') },
    signal,
  });
  return response.data;
}

export async function updateTag(
  tagId: number,
  payload: UpdateTagRequest,
  signal?: AbortSignal,
): Promise<Tag> {
  const response = await http.put<Tag>(`/api/v1/tags/${tagId}`, payload, {
    _pfhReplayAfterRefresh: true,
    signal,
  });
  return response.data;
}

export async function deleteTag(tagId: number, signal?: AbortSignal): Promise<void> {
  await http.delete(`/api/v1/tags/${tagId}`, { signal });
}

export async function restoreTag(tagId: number, signal?: AbortSignal): Promise<Tag> {
  const response = await http.post<Tag>(`/api/v1/tags/${tagId}/restore`, undefined, {
    _pfhReplayAfterRefresh: true,
    signal,
  });
  return response.data;
}
