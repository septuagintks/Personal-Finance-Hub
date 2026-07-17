import { ref } from 'vue';
import { defineStore } from 'pinia';
import {
  createCategory,
  createTag,
  deleteCategory,
  deleteTag,
  listCategories,
  listTags,
  restoreCategory,
  restoreTag,
  updateCategory,
  updateTag,
  type Category,
  type CategoryTree,
  type CreateCategoryRequest,
  type Tag,
} from '../services/metadata-api';
import { createIntentKeyTracker } from '../services/idempotency';

export const useMetadataStore = defineStore('metadata', () => {
  const categories = ref<CategoryTree[]>([]);
  const tags = ref<Tag[]>([]);
  const categoryState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  const tagState = ref<'idle' | 'loading' | 'ready' | 'error'>('idle');
  let lifecycle = 0;
  let categoryRequest = 0;
  let tagRequest = 0;
  let categoryController: AbortController | null = null;
  let tagController: AbortController | null = null;
  let actionController: AbortController | null = null;
  const categoryCreateIntent = createIntentKeyTracker('category');
  const tagCreateIntent = createIntentKeyTracker('tag');

  async function loadCategories(): Promise<boolean> {
    const currentLifecycle = lifecycle;
    const request = ++categoryRequest;
    categoryController?.abort();
    const controller = new AbortController();
    categoryController = controller;
    categoryState.value = 'loading';
    try {
      const result = await listCategories('all', controller.signal);
      if (
        lifecycle !== currentLifecycle ||
        request !== categoryRequest ||
        controller.signal.aborted
      ) {
        return false;
      }
      categories.value = result;
      categoryState.value = 'ready';
      return true;
    } catch (error) {
      if (
        lifecycle !== currentLifecycle ||
        request !== categoryRequest ||
        controller.signal.aborted
      ) {
        return false;
      }
      categories.value = [];
      categoryState.value = 'error';
      throw error;
    } finally {
      if (categoryController === controller) categoryController = null;
    }
  }

  async function loadTags(): Promise<boolean> {
    const currentLifecycle = lifecycle;
    const request = ++tagRequest;
    tagController?.abort();
    const controller = new AbortController();
    tagController = controller;
    tagState.value = 'loading';
    try {
      const result = await listTags('all', controller.signal);
      if (lifecycle !== currentLifecycle || request !== tagRequest || controller.signal.aborted) {
        return false;
      }
      tags.value = result;
      tagState.value = 'ready';
      return true;
    } catch (error) {
      if (lifecycle !== currentLifecycle || request !== tagRequest || controller.signal.aborted) {
        return false;
      }
      tags.value = [];
      tagState.value = 'error';
      throw error;
    } finally {
      if (tagController === controller) tagController = null;
    }
  }

  function beginAction(): { lifecycle: number; controller: AbortController } {
    actionController?.abort();
    const controller = new AbortController();
    actionController = controller;
    return { lifecycle, controller };
  }

  function ensureCurrent(action: { lifecycle: number; controller: AbortController }): void {
    if (
      action.lifecycle !== lifecycle ||
      actionController !== action.controller ||
      action.controller.signal.aborted
    ) {
      throw new DOMException('Metadata request is no longer current.', 'AbortError');
    }
    actionController = null;
  }

  function releaseFailedAction(action: { controller: AbortController }): void {
    if (actionController === action.controller) actionController = null;
  }

  async function createCategoryItem(payload: CreateCategoryRequest): Promise<Category> {
    const action = beginAction();
    const intentKey = categoryCreateIntent.keyFor(payload);
    let result: Category;
    try {
      result = await createCategory(payload, intentKey, action.controller.signal);
    } catch (error) {
      releaseFailedAction(action);
      throw error;
    }
    ensureCurrent(action);
    categoryCreateIntent.complete(intentKey);
    await loadCategories();
    return result;
  }

  async function updateCategoryItem(
    id: number,
    name: string,
    sortOrder: number,
  ): Promise<Category> {
    const action = beginAction();
    const result = await updateCategory(id, { name, sortOrder }, action.controller.signal);
    ensureCurrent(action);
    await loadCategories();
    return result;
  }

  async function deleteCategoryItem(id: number): Promise<void> {
    const action = beginAction();
    await deleteCategory(id, action.controller.signal);
    ensureCurrent(action);
    await loadCategories();
  }

  async function restoreCategoryItem(id: number): Promise<void> {
    const action = beginAction();
    await restoreCategory(id, action.controller.signal);
    ensureCurrent(action);
    await loadCategories();
  }

  async function createTagItem(name: string): Promise<Tag> {
    const action = beginAction();
    const payload = { name };
    const intentKey = tagCreateIntent.keyFor(payload);
    let result: Tag;
    try {
      result = await createTag(payload, intentKey, action.controller.signal);
    } catch (error) {
      releaseFailedAction(action);
      throw error;
    }
    ensureCurrent(action);
    tagCreateIntent.complete(intentKey);
    await loadTags();
    return result;
  }

  async function updateTagItem(id: number, name: string): Promise<Tag> {
    const action = beginAction();
    const result = await updateTag(id, { name }, action.controller.signal);
    ensureCurrent(action);
    await loadTags();
    return result;
  }

  async function deleteTagItem(id: number): Promise<void> {
    const action = beginAction();
    await deleteTag(id, action.controller.signal);
    ensureCurrent(action);
    await loadTags();
  }

  async function restoreTagItem(id: number): Promise<void> {
    const action = beginAction();
    await restoreTag(id, action.controller.signal);
    ensureCurrent(action);
    await loadTags();
  }

  function clear(): void {
    lifecycle += 1;
    categoryRequest += 1;
    tagRequest += 1;
    categoryController?.abort();
    tagController?.abort();
    actionController?.abort();
    categoryCreateIntent.clear();
    tagCreateIntent.clear();
    categoryController = null;
    tagController = null;
    actionController = null;
    categories.value = [];
    tags.value = [];
    categoryState.value = 'idle';
    tagState.value = 'idle';
  }

  return {
    categories,
    tags,
    categoryState,
    tagState,
    loadCategories,
    loadTags,
    createCategoryItem,
    updateCategoryItem,
    deleteCategoryItem,
    restoreCategoryItem,
    createTagItem,
    updateTagItem,
    deleteTagItem,
    restoreTagItem,
    clear,
  };
});
